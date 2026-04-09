#include <mpi.h>
#include <vector>
#include <iostream>
#include <algorithm>
#include <numeric>
using namespace std;

struct Vertex { int profit, cost; };

extern int n, m, B;
extern vector<Vertex> V;
extern vector<vector<bool>> adj;

extern int P_max;
extern vector<int> best_clique;

vector<vector<int>> greedyColor(const vector<int> &C_cand);

int colorBound(const vector<int> &C_cand);

double knapsackBound(const vector<int> &C_cand, int remainingBudget);

vector<int> intersectNeighbors(const vector<int> &C_cand, int v);

void findClique(vector<int> C_cand,
                vector<int> curr,
                int P_curr,
                int W_curr);