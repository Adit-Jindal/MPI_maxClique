#include <vector>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <numeric>
#include <mpi.h>
using namespace std;

struct Vertex {
    int profit;
    int cost;
};

int n, m, B;
int P_max = 0;

vector<Vertex> V;
vector<vector<int>> adj;

vector<int> best_clique;

// MPI tags
#define WORK_REQUEST 1
#define WORK_ASSIGN  2
#define UPDATE_BEST  3
#define TERMINATE    4

// -----------------------------
// Greedy Coloring
// -----------------------------
vector<vector<int>> greedyColor(const vector<int> &C_cand) {
    vector<int> nodes = C_cand;

    sort(nodes.begin(), nodes.end(), [&](int a, int b) {
        return V[a].profit > V[b].profit;
    });

    vector<vector<int>> colors;

    for (int v : nodes) {
        bool placed = false;

        for (auto &cls : colors) {
            bool conflict = false;
            for (int u : cls) {
                if (adj[u][v]) {
                    conflict = true;
                    break;
                }
            }
            if (!conflict) {
                cls.push_back(v);
                placed = true;
                break;
            }
        }

        if (!placed) {
            colors.push_back({v});
        }
    }

    return colors;
}

// -----------------------------
int colorBound(const vector<int> &C_cand) {
    auto colors = greedyColor(C_cand);

    int bound = 0;
    for (auto &cls : colors) {
        int best = 0;
        for (int v : cls) {
            best = max(best, V[v].profit);
        }
        bound += best;
    }
    return bound;
}

// -----------------------------
double knapsackBound(const vector<int> &C_cand, int remainingBudget) {
    vector<pair<double,int>> items;

    for (int v : C_cand) {
        double ratio = (double)V[v].profit / V[v].cost;
        items.push_back({ratio, v});
    }

    sort(items.begin(), items.end(), greater<>());

    double total = 0.0;
    int budget = remainingBudget;

    for (auto &[ratio, v] : items) {
        if (budget >= V[v].cost) {
            total += V[v].profit;
            budget -= V[v].cost;
        } else {
            total += ratio * budget;
            break;
        }
    }

    return total;
}

// -----------------------------
vector<int> intersectNeighbors(const vector<int> &C_cand, int v) {
    vector<int> res;
    for (int u : C_cand) {
        if (adj[u][v]) {
            res.push_back(u);
        }
    }
    return res;
}

// -----------------------------
// Non-blocking update check
// -----------------------------
void checkForUpdates() {
    int flag;
    MPI_Status status;

    MPI_Iprobe(0, UPDATE_BEST, MPI_COMM_WORLD, &flag, &status);
    if (flag) {
        int newP;
        MPI_Recv(&newP, 1, MPI_INT, 0, UPDATE_BEST, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        P_max = max(P_max, newP);
    }
}

// -----------------------------
// Branch & Bound
// -----------------------------
void findClique(vector<int> C_cand,
                vector<int> &current_clique,
                int P_curr,
                int W_curr) {

    checkForUpdates();

    int U_color = colorBound(C_cand);
    if (P_curr + U_color <= P_max) return;

    double U_knap = knapsackBound(C_cand, B - W_curr);
    if (P_curr + U_knap <= P_max + 1e-9) return;

    while (!C_cand.empty()) {
        checkForUpdates();

        int v = C_cand.back();
        C_cand.pop_back();

        if (W_curr + V[v].cost <= B) {

            current_clique.push_back(v);

            int newProfit = P_curr + V[v].profit;
            int newWeight = W_curr + V[v].cost;

            if (newProfit > P_max) {
                P_max = newProfit;

                // Send new best to master
                MPI_Send(&P_max, 1, MPI_INT, 0, UPDATE_BEST, MPI_COMM_WORLD);

                best_clique = current_clique;
            }

            vector<int> C_next = intersectNeighbors(C_cand, v);

            findClique(C_next, current_clique, newProfit, newWeight);

            current_clique.pop_back();
        }
    }
}

// -----------------------------
// MAIN
// -----------------------------
int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // -------------------------
    // Read input
    // -------------------------
    if (rank == 0) {
        ifstream infile(argv[1]);
        infile >> n >> m >> B;

        V.resize(n);
        for (int i = 0; i < n; i++) {
            infile >> V[i].profit >> V[i].cost;
        }
        
        adj.assign(n, vector<int>(n, 0));
        
        for (int i = 0; i < m; i++) {
            int u, v;
            infile >> u >> v;
            adj[u][v] = adj[v][u] = true;
        }
    }
    
    // Broadcast data
    MPI_Bcast(&n, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&B, 1, MPI_INT, 0, MPI_COMM_WORLD);
    
    if (rank != 0) {
        V.resize(n);
        adj.assign(n, vector<int>(n));
    }
    
    MPI_Bcast(V.data(), n * sizeof(Vertex), MPI_BYTE, 0, MPI_COMM_WORLD);
    for (int i = 0; i < n; i++) {
        MPI_Bcast(adj[i].data(), n, MPI_INT, 0, MPI_COMM_WORLD);
    }

    // -------------------------
    // MASTER
    // -------------------------
    if (rank == 0) {
        queue<int> tasks;
        for (int i = 0; i < n; i++) tasks.push(i);

        int active_workers = size - 1;

        while (active_workers > 0) {
            MPI_Status status;
            int flag;

            MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &status);

            if (!flag) continue;

            int msg;
            MPI_Recv(&msg, 1, MPI_INT, status.MPI_SOURCE,
                     status.MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            int worker = status.MPI_SOURCE;

            if (status.MPI_TAG == WORK_REQUEST) {
                if (!tasks.empty()) {
                    int v = tasks.front(); tasks.pop();
                    MPI_Send(&v, 1, MPI_INT, worker, WORK_ASSIGN, MPI_COMM_WORLD);
                    cout << v << endl;
                } else {
                    MPI_Send(NULL, 0, MPI_INT, worker, TERMINATE, MPI_COMM_WORLD);
                    active_workers--;
                }
            }
            else if (status.MPI_TAG == UPDATE_BEST) {
                if (msg > P_max) {
                    P_max = msg;

                    // Send to all workers (NO BCAST)
                    for (int i = 1; i < size; i++) {
                        MPI_Send(&P_max, 1, MPI_INT, i, UPDATE_BEST, MPI_COMM_WORLD);
                    }
                }
            }
        }

        cout << "Maximum Profit: " << P_max << "\n";
        cout << "Clique vertices: ";

        for (int v : best_clique) cout << v << " ";
        cout << endl;
    }

    // -------------------------
    // WORKERS
    // -------------------------
    else {
        while (true) {
            int dummy = 0;
            MPI_Send(&dummy, 1, MPI_INT, 0, WORK_REQUEST, MPI_COMM_WORLD);

            MPI_Status status;
            int v;

            MPI_Recv(&v, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);

            if (status.MPI_TAG == TERMINATE) break;

            vector<int> C_cand;
            for (int i = 0; i < n; i++) {
                if (adj[v][i]) C_cand.push_back(i);
            }

            vector<int> clique;
            clique.push_back(v);

            findClique(C_cand, clique, V[v].profit, V[v].cost);
        }
    }

    MPI_Finalize();
    return 0;
}