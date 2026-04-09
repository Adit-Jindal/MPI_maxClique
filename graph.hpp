#ifndef GRAPH_HPP
#define GRAPH_HPP

#include <vector>
#include <fstream>
#include <iostream>

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

#endif