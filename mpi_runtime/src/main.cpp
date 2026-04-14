#include <mpi.h>
#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <algorithm>

struct Edge {
    int to;
    int weight;
};

const int INF = 1000000000;

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
                ghost_nodes.push_back(neighbor);
            }
        }
    }

    std::set<int> unique_ghosts(ghost_nodes.begin(), ghost_nodes.end());
    ghost_nodes.assign(unique_ghosts.begin(), unique_ghosts.end());

    // ---------------- LEADER ELECTION ----------------
    std::map<int, int> leader_candidate;
    for (int node : owned_nodes) {
        leader_candidate[node] = node;
    }

    int max_rounds = total_nodes;

    for (int round = 0; round < max_rounds; ++round) {
        std::map<int, int> next_candidate = leader_candidate;

        for (int node : owned_nodes) {
            for (const auto& edge : graph[node]) {
                int neighbor = edge.to;
                if (owner_of_node(neighbor, world_size, total_nodes) == world_rank) {
                    next_candidate[node] = std::max(next_candidate[node], leader_candidate[neighbor]);
                }
            }
        }

        int local_max = -1;
        for (int node : owned_nodes) {
            local_max = std::max(local_max, leader_candidate[node]);
        }

        std::vector<int> gathered(world_size, -1);
        MPI_Allgather(&local_max, 1, MPI_INT, gathered.data(), 1, MPI_INT, MPI_COMM_WORLD);

        int global_seen_max = -1;
        for (int value : gathered) {
            global_seen_max = std::max(global_seen_max, value);
        }

        for (int node : owned_nodes) {
            next_candidate[node] = std::max(next_candidate[node], global_seen_max);
        }

        leader_candidate = next_candidate;
    }

    MPI_Barrier(MPI_COMM_WORLD);

    for (int r = 0; r < world_size; ++r) {
        if (r == world_rank) {
            std::cout << "Rank " << world_rank << " final leader candidates:\n";
            for (int node : owned_nodes) {
                std::cout << "  Node " << node << " -> Leader " << leader_candidate[node] << "\n";
            }
            std::cout << "\n";
        }
        MPI_Barrier(MPI_COMM_WORLD);
    }

    int local_ok = 1;
    for (int node : owned_nodes) {
        if (leader_candidate[node] != total_nodes - 1) {
            local_ok = 0;
        }
    }

    int global_ok = 0;
    MPI_Allreduce(&local_ok, &global_ok, 1, MPI_INT, MPI_LAND, MPI_COMM_WORLD);

    if (world_rank == 0) {
        if (global_ok) {
            std::cout << "Leader election successful: all nodes agree on leader "
                      << (total_nodes - 1) << "\n\n";
        } else {
            std::cout << "Leader election failed: not all nodes agree.\n\n";
        }
    }

    // ---------------- DISTRIBUTED DIJKSTRA ----------------
    int source = 0;
    std::vector<int> dist(total_nodes, INF);
    std::vector<int> visited(total_nodes, 0);

    if (owner_of_node(source, world_size, total_nodes) == world_rank) {
        dist[source] = 0;
    }

    int iterations = 0;

    for (int step = 0; step < total_nodes; ++step) {
        int local_best_dist = INF;
        int local_best_node = -1;

        for (int node : owned_nodes) {
            if (!visited[node] && dist[node] < local_best_dist) {
                local_best_dist = dist[node];
                local_best_node = node;
            }
        }

        std::vector<int> all_best_dist(world_size, INF);
        std::vector<int> all_best_node(world_size, -1);

        MPI_Allgather(&local_best_dist, 1, MPI_INT,
                      all_best_dist.data(), 1, MPI_INT, MPI_COMM_WORLD);
        MPI_Allgather(&local_best_node, 1, MPI_INT,
                      all_best_node.data(), 1, MPI_INT, MPI_COMM_WORLD);

        int global_best_dist = INF;
        int global_best_node = -1;

        for (int i = 0; i < world_size; ++i) {
            if (all_best_dist[i] < global_best_dist) {
                global_best_dist = all_best_dist[i];
                global_best_node = all_best_node[i];
            }
        }

        if (global_best_node == -1 || global_best_dist == INF) {
            break;
        }

        visited[global_best_node] = 1;
        iterations++;

        std::vector<int> proposed(total_nodes, INF);

        if (owner_of_node(global_best_node, world_size, total_nodes) == world_rank) {
            for (const auto& edge : graph[global_best_node]) {
                int neighbor = edge.to;
                if (!visited[neighbor]) {
                    proposed[neighbor] = global_best_dist + edge.weight;
                }
            }
        }

        std::vector<int> global_proposed(total_nodes, INF);
        MPI_Allreduce(proposed.data(), global_proposed.data(), total_nodes, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

        for (int node = 0; node < total_nodes; ++node) {
            if (global_proposed[node] < dist[node]) {
                dist[node] = global_proposed[node];
            }
        }
    }

    MPI_Barrier(MPI_COMM_WORLD);

    for (int r = 0; r < world_size; ++r) {
        if (r == world_rank) {
            std::cout << "Rank " << world_rank << " final distances:\n";
            for (int node : owned_nodes) {
                if (dist[node] >= INF) {
                    std::cout << "  Node " << node << " -> INF\n";
                } else {
                    std::cout << "  Node " << node << " -> " << dist[node] << "\n";
                }
            }
            std::cout << "\n";
        }
        MPI_Barrier(MPI_COMM_WORLD);
    }

    if (world_rank == 0) {
        std::cout << "Dijkstra finished in " << iterations << " iterations\n";
        std::cout << "Expected distances from source 0:\n";
        std::cout << "  Node 0 -> 0\n";
        std::cout << "  Node 1 -> 1\n";
        std::cout << "  Node 2 -> 3\n";
        std::cout << "  Node 3 -> 6\n";
    }

    MPI_Finalize();
    return 0;
}