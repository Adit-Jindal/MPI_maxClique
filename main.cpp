#include <mpi.h>
#include <vector>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <fstream>
#include <deque>
#include <random>

using namespace std;

using ull = uint64_t;

// ---------- Bitset ----------
struct Bitset {
    vector<ull> b;
    int n;

    Bitset(int n = 0) : n(n) {
        b.assign((n + 63) >> 6, 0);
    }

    void set(int i) { b[i >> 6] |= (1ULL << (i & 63)); }
    void reset(int i) { b[i >> 6] &= ~(1ULL << (i & 63)); }
    bool test(int i) const { return b[i >> 6] & (1ULL << (i & 63)); }

    void intersect(const Bitset& other) {
        for (size_t i = 0; i < b.size(); i++) b[i] &= other.b[i];
    }

    bool empty() const {
        for (auto x : b) if (x) return false;
        return true;
    }

    int pop_last() {
        for (int i = (int)b.size() - 1; i >= 0; i--) {
            if (b[i]) {
                int bit = 63 - __builtin_clzll(b[i]);
                int v = (i << 6) + bit;
                b[i] ^= (1ULL << bit);
                return v;
            }
        }
        return -1;
    }

    vector<int> to_vector() const {
        vector<int> res;
        for (int i = 0; i < (int)b.size(); i++) {
            ull x = b[i];
            while (x) {
                int bit = __builtin_ctzll(x);
                res.push_back((i << 6) + bit);
                x &= x - 1;
            }
        }
        return res;
    }
};

// ---------- Graph ----------
struct Graph {
    int N, M;
    long long B;

    vector<long long> profit, cost;
    vector<Bitset> adj;

    bool load(const char* file) {
        ifstream in(file);
        if (!in) return false;

        in >> N >> M >> B;
        profit.resize(N);
        cost.resize(N);
        adj.assign(N, Bitset(N));

        for (int i = 0; i < N; i++)
            in >> profit[i] >> cost[i];

        for (int i = 0; i < M; i++) {
            int u, v;
            in >> u >> v;
            adj[u].set(v);
            adj[v].set(u);
        }
        return true;
    }
};

// ---------- Globals ----------
static const int TAG_BOUND = 1;
static const int CHECK_INTERVAL = 500;

static long long P_max;
static vector<int> best_clique;
static const Graph* G;
static int mpi_rank, mpi_size;
static int check_counter;

static deque<long long> send_bufs;
static vector<MPI_Request> send_reqs;

// ---------- MPI ----------
static void share_bound(long long bound) {
    for (int r = 0; r < mpi_size; r++) {
        if (r != mpi_rank) {
            send_bufs.push_back(bound);
            MPI_Request req;
            MPI_Isend(&send_bufs.back(), 1, MPI_LONG_LONG, r, TAG_BOUND, MPI_COMM_WORLD, &req);
            send_reqs.push_back(req);
        }
    }
}

static void check_incoming_bounds() {
    int flag;
    MPI_Status st;
    while (true) {
        MPI_Iprobe(MPI_ANY_SOURCE, TAG_BOUND, MPI_COMM_WORLD, &flag, &st);
        if (!flag) break;
        long long val;
        MPI_Recv(&val, 1, MPI_LONG_LONG, st.MPI_SOURCE, TAG_BOUND, MPI_COMM_WORLD, &st);
        if (val > P_max) P_max = val;
    }
}

// ---------- Coloring Bound ----------
static double ColorBound(const Bitset& C) {
    auto nodes = C.to_vector();

    sort(nodes.begin(), nodes.end(), [](int a, int b) {
        return G->profit[a] > G->profit[b];
    });

    vector<vector<int>> colors;

    for (int v : nodes) {
        bool placed = false;
        for (auto& cls : colors) {
            bool ok = true;
            for (int u : cls) {
                if (G->adj[v].test(u)) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                cls.push_back(v);
                placed = true;
                break;
            }
        }
        if (!placed) colors.push_back({v});
    }

    double bound = 0;
    for (auto& cls : colors) {
        long long best = 0;
        for (int v : cls)
            best = max(best, G->profit[v]);
        bound += best;
    }
    return bound;
}

// ---------- Knapsack ----------
static double KnapsackBound(const Bitset& C, long long remaining) {
    if (remaining <= 0) return 0;

    auto nodes = C.to_vector();
    sort(nodes.begin(), nodes.end(), [](int a, int b) {
        return G->profit[a] * G->cost[b] > G->profit[b] * G->cost[a];
    });

    double U = 0;
    long long left = remaining;

    for (int v : nodes) {
        if (G->cost[v] <= left) {
            U += G->profit[v];
            left -= G->cost[v];
        } else {
            U += (double)G->profit[v] * left / G->cost[v];
            break;
        }
    }
    return U;
}

// ---------- BnB ----------
static void FindClique(Bitset C, long long P, long long W, vector<int> clique) {
    if (++check_counter % CHECK_INTERVAL == 0)
        check_incoming_bounds();

    double U_color = ColorBound(C);
    if (P + U_color <= P_max) return;

    double U_knap = KnapsackBound(C, G->B - W);
    if (P + U_knap <= P_max) return;

    while (!C.empty()) {
        int v = C.pop_last();
        if (v == -1) break;

        if (W + G->cost[v] > G->B) continue;

        long long newP = P + G->profit[v];

        if (newP > P_max) {
            P_max = newP;
            best_clique = clique;
            best_clique.push_back(v);
            share_bound(P_max);
        }

        Bitset next = C;
        next.intersect(G->adj[v]);

        auto new_clique = clique;
        new_clique.push_back(v);

        FindClique(next, newP, W + G->cost[v], new_clique);
    }
}

// ---------- Subproblem ----------
struct Subproblem {
    Bitset C;
    vector<int> clique;
    long long P, W;
};

// ---------- Generate ----------
static vector<Subproblem> generate(const Graph& g) {
    vector<Subproblem> subs;

    for (int v = 0; v < g.N; v++) {
        if (g.cost[v] > g.B) continue;

        Bitset C1 = g.adj[v];
        subs.push_back({C1, {v}, g.profit[v], g.cost[v]});
    }
    return subs;
}

// ---------- MAIN ----------
int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);

    // ---- START TIMER ----
    double start_time = MPI_Wtime();

    Graph g;
    g.load(argv[1]);
    G = &g;

    P_max = 0;
    check_counter = 0;

    long long global_init;
    MPI_Allreduce(&P_max, &global_init, 1, MPI_LONG_LONG, MPI_MAX, MPI_COMM_WORLD);
    P_max = global_init;

    auto subs = generate(g);

    for (int i = 0; i < (int)subs.size(); i++) {
        if (i % mpi_size != mpi_rank) continue;

        auto& s = subs[i];
        FindClique(s.C, s.P, s.W, s.clique);
    }

    long long global;
    MPI_Allreduce(&P_max, &global, 1, MPI_LONG_LONG, MPI_MAX, MPI_COMM_WORLD);

    // ---- END TIMER ----
    double end_time = MPI_Wtime();
    double local_time = end_time - start_time;

    double max_time;
    MPI_Reduce(&local_time, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    if (mpi_rank == 0) {
        cout << "P_max = " << global << endl;
        cout << "Time : " << max_time << " seconds" << endl;
    }

    MPI_Finalize();
}