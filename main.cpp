
/*
 * MPI Version 2: Depth-2 Expansion + Static Round-Robin + Bound Sharing + Initial Heuristic
 *
 * Strategy:
 *   1. All ranks run a randomized greedy heuristic (different seeds) to get a
 *      strong initial P_max. Allreduce picks the global best before exact search.
 *   2. All ranks independently expand the B&B tree to depth 2, generating the
 *      same set of subproblems (with pruning against the initial P_max).
 *   3. Subproblems are sorted by estimated difficulty (descending candidate set
 *      size) and distributed round-robin for load balance.
 *   4. Each rank solves its assigned subproblems with async bound sharing.
 *
 * Pros:  Better balance than V1 (many subprobs per rank); strong initial bound
 *        from heuristic means heavy pruning from the start.
 * Cons:  Static assignment still can't adapt to runtime imbalance.
 */

#include <mpi.h>
#include <vector>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <fstream>
#include <deque>
#include <random>
#include <tuple>

using namespace std;

struct Graph {
    int N; // number of vertices
    int M; // number of edges
    long long B; // budget

    vector<long long> profit;
    vector<long long> cost;
    vector<vector<bool>> adj;

    bool load(const char* filename) {
        ifstream infile(filename);
        if (!infile) {
            cerr << "Error opening file\n";
            return false;
        }

        infile >> N >> M >> B;

        profit.resize(N);
        cost.resize(N);
        adj.assign(N, vector<bool>(N, false));

        // read vertex data
        for (int i = 0; i < N; i++) {
            infile >> profit[i] >> cost[i];
        }

        // read edges
        for (int i = 0; i < M; i++) {
            int u, v;
            infile >> u >> v;
            adj[u][v] = true;
            adj[v][u] = true;
        }

        return true;
    }
};


static const int TAG_BOUND = 1;
static const int CHECK_INTERVAL = 500;

static long long P_max;
static vector<int> best_clique;
static const Graph* G;
static int mpi_rank, mpi_size;
static int check_counter;

static deque<long long> send_bufs;
static vector<MPI_Request> send_reqs;

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
    MPI_Status status;
    while (true) {
        MPI_Iprobe(MPI_ANY_SOURCE, TAG_BOUND, MPI_COMM_WORLD, &flag, &status);
        if (!flag) break;
        long long received;
        MPI_Recv(&received, 1, MPI_LONG_LONG, status.MPI_SOURCE, TAG_BOUND, MPI_COMM_WORLD, &status);
        if (received > P_max) P_max = received;
    }
}

static void cleanup_sends() {
    if (!send_reqs.empty()) {
        MPI_Waitall(send_reqs.size(), send_reqs.data(), MPI_STATUSES_IGNORE);
        send_reqs.clear();
        send_bufs.clear();
    }
}

// ---------- Core B&B ----------

static vector<vector<int>> GreedyColor(const vector<int>& C_cand) {
    vector<int> sorted = C_cand;
    sort(sorted.begin(), sorted.end(), [](int a, int b) {
        return G->profit[a] > G->profit[b];
    });
    vector<vector<int>> Colors;
    for (int v : sorted) {
        bool placed = false;
        for (auto& c : Colors) {
            bool ok = true;
            for (int u : c) if (G->adj[v][u]) { ok = false; break; }
            if (ok) { c.push_back(v); placed = true; break; }
        }
        if (!placed) Colors.push_back({v});
    }
    return Colors;
}

static double KnapsackBound(const vector<int>& C_cand, long long remaining) {
    if (remaining <= 0) return 0.0;
    vector<int> sorted = C_cand;
    sort(sorted.begin(), sorted.end(), [](int a, int b) {
        return G->profit[a] * G->cost[b] > G->profit[b] * G->cost[a];
    });
    double U = 0.0;
    long long left = remaining;
    for (int v : sorted) {
        if (G->cost[v] <= left) { U += G->profit[v]; left -= G->cost[v]; }
        else { U += (double)G->profit[v] * left / G->cost[v]; break; }
    }
    return U;
}

static void FindClique(vector<int> C_cand, long long P_curr, long long W_curr, vector<int> curr_clique) {
    if (++check_counter % CHECK_INTERVAL == 0)
        check_incoming_bounds();
        
    auto Colors = GreedyColor(C_cand);
    double U_color = 0;
    for (auto& c : Colors) {
        long long best = 0;
        for (int v : c) best = max(best, G->profit[v]);
        U_color += best;
    }
    if (P_curr + U_color <= (double)P_max) return;

    double U_knap = KnapsackBound(C_cand, G->B - W_curr);
    if (P_curr + U_knap <= (double)P_max) return;

    while (!C_cand.empty()) {
        int v = C_cand.back();
        C_cand.pop_back();

        if (W_curr + G->cost[v] <= G->B) {
            long long new_P = P_curr + G->profit[v];
            if (new_P > P_max) {
                P_max = new_P;
                best_clique = curr_clique;
                best_clique.push_back(v);
                share_bound(P_max);
            }

            vector<int> C_next;
            for (int u : C_cand)
                if (G->adj[v][u]) C_next.push_back(u);

            vector<int> next_clique = curr_clique;
            next_clique.push_back(v);
            FindClique(C_next, new_P, W_curr + G->cost[v], next_clique);
        }
    }
}

// ---------- Randomized Greedy Heuristic ----------
// Build a greedy clique: iteratively add the best-ratio vertex that is
// adjacent to all current clique members and fits the budget.
// Random perturbation of profit/cost ratios for diversity across ranks.

static pair<long long, vector<int>> greedy_heuristic(const Graph& g, mt19937& rng) {
    vector<int> order(g.N);
    iota(order.begin(), order.end(), 0);

    // Perturbed sort by profit/cost ratio
    uniform_real_distribution<double> noise(0.8, 1.2);
    vector<double> score(g.N);
    for (int i = 0; i < g.N; i++)
        score[i] = (double)g.profit[i] / g.cost[i] * noise(rng);
    sort(order.begin(), order.end(), [&](int a, int b) { return score[a] > score[b]; });

    vector<int> clique;
    long long profit = 0, weight = 0;

    for (int v : order) {
        if (weight + g.cost[v] > g.B) continue;
        bool fits = true;
        for (int u : clique)
            if (!g.adj[v][u]) { fits = false; break; }
        if (fits) {
            clique.push_back(v);
            profit += g.profit[v];
            weight += g.cost[v];
        }
    }
    return {profit, clique};
}

// ---------- Subproblem representation ----------
struct Subproblem {
    vector<int> C_cand;
    vector<int> clique;
    long long P_curr;
    long long W_curr;
};

// Generate all depth-2 subproblems from the B&B root.
// Uses the same branching logic as FindClique but stops at depth 2.
static vector<Subproblem> generate_subproblems(const Graph& g) {
    vector<Subproblem> subs;

    // Depth-1: iterate root candidates
    vector<int> root_cand(g.N);
    iota(root_cand.begin(), root_cand.end(), 0);

    for (int i = (int)root_cand.size() - 1; i >= 0; i--) {
        int v = root_cand[i];
        if (g.cost[v] > g.B) continue;

        long long P1 = g.profit[v];
        long long W1 = g.cost[v];

        // C_next at depth 1: neighbors of v among [0..v-1]
        vector<int> C1;
        for (int u = 0; u < i; u++)
            if (g.adj[v][root_cand[u]]) C1.push_back(root_cand[u]);

        if (C1.empty()) {
            // Leaf at depth 1: this is a complete subproblem (single-vertex clique)
            // Just check if it beats P_max (handled during solve)
            subs.push_back({C1, {v}, P1, W1});
            continue;
        }

        // Depth-2: iterate candidates within this branch
        for (int j = (int)C1.size() - 1; j >= 0; j--) {
            int w = C1[j];
            if (W1 + g.cost[w] > g.B) continue;

            long long P2 = P1 + g.profit[w];
            long long W2 = W1 + g.cost[w];

            // C_next at depth 2: neighbors of w among C1[0..j-1]
            vector<int> C2;
            for (int k = 0; k < j; k++)
                if (g.adj[w][C1[k]]) C2.push_back(C1[k]);

            subs.push_back({C2, {v, w}, P2, W2});
        }

        // Also add the depth-1 "don't pick any depth-2 vertex" case?
        // No — the sequential code's while-loop covers all choices including
        // "skip v" implicitly by continuing the loop. The depth-2 expansion
        // above already generates all sub-branches. But we still need the
        // depth-1 subproblem for the case where v is alone:
        // Actually, the depth-2 expansions above cover all children.
        // The depth-1 node with v alone is only a leaf if C1 is empty,
        // which we handled above. So we're fine.
    }

    return subs;
}

int main(int argc, char* argv[]) {
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_size);

    if (argc < 3) {
        if (mpi_rank == 0) cerr << "Usage: " << argv[0] << " <input> <output>\n";
        MPI_Finalize(); return 1;
    }

    Graph g;
    if (!g.load(argv[1])) { MPI_Finalize(); return 1; }
    G = &g;
    P_max = 0;
    best_clique.clear();
    check_counter = 0;

    double t_start = MPI_Wtime();

    // ---------- Phase 1: Parallel greedy heuristic ----------
    {
        mt19937 rng(42 + mpi_rank * 1000);
        int trials = max(50, 500 / mpi_size); // each rank does this many trials
        for (int t = 0; t < trials; t++) {
            auto [profit, clique] = greedy_heuristic(g, rng);
            if (profit > P_max) {
                P_max = profit;
                best_clique = clique;
            }
        }
    }

    // Allreduce to get global best heuristic bound
    long long heur_global;
    MPI_Allreduce(&P_max, &heur_global, 1, MPI_LONG_LONG, MPI_MAX, MPI_COMM_WORLD);
    // Find winner and broadcast its clique
    {
        int winner = (P_max == heur_global) ? mpi_rank : mpi_size;
        int gw;
        MPI_Allreduce(&winner, &gw, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
        int cs = (mpi_rank == gw) ? (int)best_clique.size() : 0;
        MPI_Bcast(&cs, 1, MPI_INT, gw, MPI_COMM_WORLD);
        if (mpi_rank != gw) best_clique.resize(cs);
        MPI_Bcast(best_clique.data(), cs, MPI_INT, gw, MPI_COMM_WORLD);
        P_max = heur_global;
    }

    if (mpi_rank == 0)
        cout << "Heuristic P_max = " << P_max << "\n";

    // ---------- Phase 2: Generate subproblems (all ranks, deterministic) ----------
    auto subs = generate_subproblems(g);

    // Sort by descending candidate set size (larger = harder = process first)
    // Then round-robin assignment distributes hard tasks evenly.
    sort(subs.begin(), subs.end(), [](const Subproblem& a, const Subproblem& b) {
        return a.C_cand.size() > b.C_cand.size();
    });

    if (mpi_rank == 0)
        cout << "Generated " << subs.size() << " depth-2 subproblems\n";

    // ---------- Phase 3: Solve assigned subproblems ----------
    for (int i = 0; i < (int)subs.size(); i++) {
        if (i % mpi_size != mpi_rank) continue;

        auto& sp = subs[i];

        // Check if this subproblem's clique already beats P_max
        if (sp.P_curr > P_max) {
            P_max = sp.P_curr;
            best_clique = sp.clique;
            share_bound(P_max);
        }

        if (!sp.C_cand.empty()) {
            FindClique(sp.C_cand, sp.P_curr, sp.W_curr, sp.clique);
        }
    }

    check_incoming_bounds();

    // ---------- Gather global best ----------
    long long global_P_max;
    MPI_Allreduce(&P_max, &global_P_max, 1, MPI_LONG_LONG, MPI_MAX, MPI_COMM_WORLD);
    {
        int winner = (P_max == global_P_max) ? mpi_rank : mpi_size;
        int gw;
        MPI_Allreduce(&winner, &gw, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
        int cs = (mpi_rank == gw) ? (int)best_clique.size() : 0;
        MPI_Bcast(&cs, 1, MPI_INT, gw, MPI_COMM_WORLD);
        if (mpi_rank != gw) best_clique.resize(cs);
        MPI_Bcast(best_clique.data(), cs, MPI_INT, gw, MPI_COMM_WORLD);
    }

    double t_end = MPI_Wtime();

    if (mpi_rank == 0) {
        ofstream out(argv[2]);
        out << global_P_max << "\n";
        for (int i = 0; i < (int)best_clique.size(); i++) {
            if (i) out << " ";
            out << best_clique[i];
        }
        out << "\n";

        cout << "P_max = " << global_P_max << "\n";
        cout << "Clique:";
        for (int v : best_clique) cout << " " << v;
        cout << "\nTime: " << (t_end - t_start) << " sec, Ranks: " << mpi_size << "\n";
    }

    cleanup_sends();
    MPI_Finalize();
    return 0;
}

