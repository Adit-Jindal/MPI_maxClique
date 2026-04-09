#include "shared.h"



int main(int argc, char** argv) {
    if (argc!=3) {
        cerr << "Usage format takes arguments - input.txt output.txt\n";
        return 1;
    }
    ifstream infile(argv[1]);
    ofstream outfile(argv[2]);

    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // assume input read on root
    if (rank == 0) {
        infile >> n >> m >> B;
        V.resize(n);
        for (int i = 0; i < n; i++) infile >> V[i].profit >> V[i].cost;

        adj.assign(n, vector<bool>(n,false));
        for (int i = 0,u,v;i<m;i++){
            infile>>u>>v;
            adj[u][v]=adj[v][u]=true;
        }
    }

    // broadcast data
    MPI_Bcast(&n,1,MPI_INT,0,MPI_COMM_WORLD);
    MPI_Bcast(&B,1,MPI_INT,0,MPI_COMM_WORLD);

    // (broadcast V and adj properly in real code)

    vector<int> C_cand(n);
    iota(C_cand.begin(), C_cand.end(), 0);

    // each process handles vertices i = rank, rank+size, ...
    for (int i = rank; i < n; i += size) {
        int v = i;

        if (V[v].cost <= B) {
            vector<int> curr = {v};
            vector<int> C_next = intersectNeighbors(C_cand, v);

            findClique(C_next, curr, V[v].profit, V[v].cost);
        }
    }

    int global_best;
    MPI_Reduce(&P_max, &global_best, 1, MPI_INT, MPI_MAX, 0, MPI_COMM_WORLD);

    if (rank == 0) cout << "Max Profit: " << global_best << endl;

    MPI_Finalize();
}

/*

int counter = 0;

void findClique(...) {

    counter++;

    if (counter % 1000 == 0) {
        int global_best;
        MPI_Allreduce(&P_max, &global_best, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
        P_max = global_best;
    }

    ...
}

*/



/*

#define REQUEST 1
#define WORK 2

void worker_loop(vector<vector<int>> &tasks, int rank, int size) {

    while (true) {

        if (!tasks.empty()) {
            auto C = tasks.back(); tasks.pop_back();
            findClique(C, {}, 0, 0);
        } else {
            // request work
            int dummy = 0;
            MPI_Send(&dummy, 1, MPI_INT, 0, REQUEST, MPI_COMM_WORLD);

            MPI_Status status;
            vector<int> new_task(1000);

            MPI_Recv(new_task.data(), 1000, MPI_INT, 0, WORK, MPI_COMM_WORLD, &status);

            if (new_task[0] == -1) break;

            tasks.push_back(new_task);
        }
    }
}

if (rank == 0) {
    vector<vector<int>> tasks;

    // generate initial tasks
    for (int i = 0; i < n; i++) {
        tasks.push_back({i});
    }

    while (!tasks.empty()) {
        MPI_Status status;
        int dummy;

        MPI_Recv(&dummy, 1, MPI_INT, MPI_ANY_SOURCE, REQUEST, MPI_COMM_WORLD, &status);

        int worker = status.MPI_SOURCE;

        vector<int> task = tasks.back();
        tasks.pop_back();

        MPI_Send(task.data(), task.size(), MPI_INT, worker, WORK, MPI_COMM_WORLD);
    }

    // send termination
    for (int i = 1; i < size; i++) {
        int end_signal = -1;
        MPI_Send(&end_signal, 1, MPI_INT, i, WORK, MPI_COMM_WORLD);
    }
}

*/






/*

vector<vector<int>> tasks;

for (int i = 0; i < n; i++) {
    for (int j = i+1; j < n; j++) {
        if (adj[i][j]) {
            tasks.push_back({i,j});
        }
    }
}



int per_proc = (tasks.size() + size - 1)/size;

vector<vector<int>> local_tasks;

for (int i = rank*per_proc; i < min((rank+1)*per_proc, (int)tasks.size()); i++) {
    local_tasks.push_back(tasks[i]);
}




for (auto &t : local_tasks) {
    vector<int> C_cand(n);
    iota(C_cand.begin(), C_cand.end(), 0);

    vector<int> curr = t;

    int P=0,W=0;
    for(int v:t){
        P+=V[v].profit;
        W+=V[v].cost;
    }

    vector<int> C_next = C_cand;
    for(int v:t){
        C_next = intersectNeighbors(C_next,v);
    }

    findClique(C_next, curr, P, W);
}




periodic sync as in strat 2


*/