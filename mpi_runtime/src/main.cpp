#include <mpi.h>
#include <iostream>
#include <vector>
#include <map>

struct Edge {
    int to;
    int weight;
};

int owner_of_node(int node_id, int world_size, int total_nodes) {
    int nodes_per_rank = (total_nodes + world_size - 1) / world_size;
    return node_id / nodes_per_rank;
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int world_size = 0;
    int world_rank = 0;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    // Tiny hardcoded graph:
    // 0 --1-- 1 --2-- 2 --3-- 3
    const int total_nodes = 4;
    std::map<int, std::vector<Edge>> graph;
    graph[0] = {{1, 1}};
    graph[1] = {{0, 1}, {2, 2}};
    graph[2] = {{1, 2}, {3, 3}};
    graph[3] = {{2, 3}};

    std::vector<int> owned_nodes;
    std::vector<int> ghost_nodes;

    for (int node = 0; node < total_nodes; ++node) {
        if (owner_of_node(node, world_size, total_nodes) == world_rank) {
            owned_nodes.push_back(node);
        }
    }

    for (int node : owned_nodes) {
        for (const auto& edge : graph[node]) {
            int neighbor = edge.to;
            if (owner_of_node(neighbor, world_size, total_nodes) != world_rank) {
                bool already_present = false;
                for (int ghost : ghost_nodes) {
                    if (ghost == neighbor) {
                        already_present = true;
                        break;
                    }
                }
                if (!already_present) {
                    ghost_nodes.push_back(neighbor);
                }
            }
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    for (int r = 0; r < world_size; ++r) {
        if (r == world_rank) {
            std::cout << "Rank " << world_rank << " owns nodes: ";
            for (int node : owned_nodes) {
                std::cout << node << " ";
            }
            std::cout << "\n";

            std::cout << "Rank " << world_rank << " ghost nodes: ";
            for (int node : ghost_nodes) {
                std::cout << node << " ";
            }
            std::cout << "\n\n";
        }
        MPI_Barrier(MPI_COMM_WORLD);
    }

    MPI_Finalize();
    return 0;
}