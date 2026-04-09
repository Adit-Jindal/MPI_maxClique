#include <bits/stdc++.h>
using namespace std;

struct Vertex {
    int profit;
    int cost;
};

int n, m, B;
int P_max = 0;

vector<Vertex> V;
vector<vector<bool>> adj;

vector<int> best_clique; // store best solution

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
// Structural Bound
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
// Knapsack Bound
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
// Neighbor intersection
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
// Branch & Bound
// -----------------------------
void findClique(vector<int> C_cand,
                vector<int> current_clique,
                int P_curr,
                int W_curr) {

    // --- Structural Bound ---
    int U_color = colorBound(C_cand);
    if (P_curr + U_color <= P_max) return;

    // --- Knapsack Bound ---
    double U_knap = knapsackBound(C_cand, B - W_curr);
    if (P_curr + U_knap <= P_max + 1e-9) return;

    // --- Branching ---
    while (!C_cand.empty()) {
        int v = C_cand.back();
        C_cand.pop_back();

        if (W_curr + V[v].cost <= B) {

            vector<int> new_clique = current_clique;
            new_clique.push_back(v);

            int newProfit = P_curr + V[v].profit;
            int newWeight = W_curr + V[v].cost;

            if (newProfit > P_max) {
                P_max = newProfit;
                best_clique = new_clique;
            }

            vector<int> C_next = intersectNeighbors(C_cand, v);

            findClique(C_next, new_clique, newProfit, newWeight);
        }
    }
}

// -----------------------------
// Main
// -----------------------------
int main() {
    ifstream infile("input.txt");
    infile >> n >> m >> B;

    V.resize(n);
    for (int i = 0; i < n; i++) {
        infile >> V[i].profit >> V[i].cost;
    }

    adj.assign(n, vector<bool>(n, false));

    for (int i = 0; i < m; i++) {
        int u, v;
        infile >> u >> v;
        adj[u][v] = adj[v][u] = true;
    }

    vector<int> C_cand(n);
    iota(C_cand.begin(), C_cand.end(), 0);

    vector<int> current_clique;

    findClique(C_cand, current_clique, 0, 0);

    // Output
    cout << "Maximum Profit: " << P_max << "\n";
    cout << "Clique vertices: ";

    reverse(best_clique.begin(), best_clique.end());

    for (int v : best_clique) {
        cout << v << " ";
    }
    cout << endl;
}