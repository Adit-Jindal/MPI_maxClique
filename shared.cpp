#include "shared.h"


int n, m, B;
vector<Vertex> V;
vector<vector<bool>> adj;

int P_max = 0;
vector<int> best_clique;


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


// IMPORTANT: make P_max LOCAL per process
void findClique(vector<int> C_cand,
                vector<int> curr,
                int P_curr,
                int W_curr) {

    int U_color = colorBound(C_cand);
    if (P_curr + U_color <= P_max) return;

    double U_knap = knapsackBound(C_cand, B - W_curr);
    if (P_curr + U_knap <= P_max + 1e-9) return;

    while (!C_cand.empty()) {
        int v = C_cand.back();
        C_cand.pop_back();

        if (W_curr + V[v].cost <= B) {
            auto new_clique = curr;
            new_clique.push_back(v);

            int newP = P_curr + V[v].profit;
            int newW = W_curr + V[v].cost;

            if (newP > P_max) {
                P_max = newP;
                best_clique = new_clique;
            }

            vector<int> C_next = intersectNeighbors(C_cand, v);
            findClique(C_next, new_clique, newP, newW);
        }
    }
}