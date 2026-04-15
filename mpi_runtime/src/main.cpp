#include <mpi.h>
#include <iostream>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <string>
#include <fstream>
#include <sstream>

struct Edge {
    int to;
    int weight;
};

const int INF = 1000000000;

std::string get_arg_value(int argc, char** argv, const std::string& key) {
    for (int i = 1; i < argc - 1; ++i) {
        if (std::string(argv[i]) == key) {
            return argv[i + 1];
        }
    }
    return "";
}

void print_usage(int world_rank) {
    if (world_rank == 0) {
        std::cout << "Usage:\n";
        std::cout << "  ./ngs_mpi --graph outputs/graph.txt --part outputs/part.txt --algo leader\n";
        std::cout << "  ./ngs_mpi --graph outputs/graph.txt --part outputs/part.txt --algo dijkstra --source 0\n";
    }
}

bool load_graph(const std::string& filename,
                int& total_nodes,
                std::map<int, std::vector<Edge>>& graph) {
    std::ifstream fin(filename);
    if (!fin.is_open()) {
        return false;
    }

    fin >> total_nodes;
    if (fin.fail() || total_nodes <= 0) {
        return false;
    }

    graph.clear();
    for (int i = 0; i < total_nodes; ++i) {
        graph[i] = {};
    }

    int u, v, w;
    while (fin >> u >> v >> w) {
        if (u < 0 || u >= total_nodes || v < 0 || v >= total_nodes || w <= 0) {
            return false;
        }
        graph[u].push_back({v, w});
        graph[v].push_back({u, w});
    }

    return true;
}

bool load_partition(const std::string& filename,
                    int total_nodes,
                    std::map<int, int>& owner_map) {
    std::ifstream fin(filename);
    if (!fin.is_open()) {
        return false;
    }

    owner_map.clear();

    int node, owner;
    while (fin >> node >> owner) {
        if (node < 0 || node >= total_nodes || owner < 0) {
            return false;
        }
        owner_map[node] = owner;
    }

    if ((int)owner_map.size() != total_nodes) {
        return false;
    }

    return true;
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);

    int world_size = 0;
    int world_rank = 0;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

    std::string graph_file = get_arg_value(argc, argv, "--graph");
    std::string part_file = get_arg_value(argc, argv, "--part");
    std::string algo = get_arg_value(argc, argv, "--algo");
    std::string source_str = get_arg_value(argc, argv, "--source");

    if (graph_file.empty() || part_file.empty() || algo.empty()) {
        print_usage(world_rank);
        MPI_Finalize();
        return 1;
    }

    int source = 0;
    if (!source_str.empty()) {
        source = std::stoi(source_str);
    }

    int total_nodes = 0;
    std::map<int, std::vector<Edge>> graph;
    std::map<int, int> owner_map;

    bool graph_ok = load_graph(graph_file, total_nodes, graph);
    bool part_ok = false;
    if (graph_ok) {
        part_ok = load_partition(part_file, total_nodes, owner_map);
    }

    if (!graph_ok) {
        if (world_rank == 0) {
            std::cout << "Failed to load graph file: " << graph_file << "\n";
        }
        MPI_Finalize();
        return 1;
    }

    if (!part_ok) {
        if (world_rank == 0) {
            std::cout << "Failed to load partition file: " << part_file << "\n";
        }
        MPI_Finalize();
        return 1;
    }

    for (int node = 0; node < total_nodes; ++node) {
        if (owner_map[node] >= world_size) {
            if (world_rank == 0) {
                std::cout << "Partition file contains owner rank >= world_size for node "
                          << node << "\n";
            }
            MPI_Finalize();
            return 1;
        }
    }

    if (source < 0 || source >= total_nodes) {
        if (world_rank == 0) {
            std::cout << "Invalid source node: " << source << "\n";
        }
        MPI_Finalize();
        return 1;
    }

    std::vector<int> owned_nodes;
    std::vector<int> ghost_nodes;

    for (int node = 0; node < total_nodes; ++node) {
        if (owner_map[node] == world_rank) {
            owned_nodes.push_back(node);
        }
    }

    for (int node : owned_nodes) {
        for (const auto& edge : graph[node]) {
            int neighbor = edge.to;
            if (owner_map[neighbor] != world_rank) {
                ghost_nodes.push_back(neighbor);
            }
        }
    }

    std::set<int> unique_ghosts(ghost_nodes.begin(), ghost_nodes.end());
    ghost_nodes.assign(unique_ghosts.begin(), unique_ghosts.end());

    if (algo == "leader") {
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
                    if (owner_map[neighbor] == world_rank) {
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

        int expected_leader = total_nodes - 1;
        int local_ok = 1;
        for (int node : owned_nodes) {
            if (leader_candidate[node] != expected_leader) {
                local_ok = 0;
            }
        }

        int global_ok = 0;
        MPI_Allreduce(&local_ok, &global_ok, 1, MPI_INT, MPI_LAND, MPI_COMM_WORLD);

        if (world_rank == 0) {
            if (global_ok) {
                std::cout << "Leader election successful: all nodes agree on leader "
                          << expected_leader << "\n";
            } else {
                std::cout << "Leader election failed: not all nodes agree.\n";
            }
        }
    }
    else if (algo == "dijkstra") {
        std::vector<int> dist(total_nodes, INF);
        std::vector<int> visited(total_nodes, 0);

        if (owner_map[source] == world_rank) {
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

            if (owner_map[global_best_node] == world_rank) {
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
                std::cout << "Rank " << world_rank << " final distances from source " << source << ":\n";
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
        }
    }
    else {
        if (world_rank == 0) {
            std::cout << "Unknown algorithm: " << algo << "\n";
        }
        print_usage(world_rank);
        MPI_Finalize();
        return 1;
    }

    MPI_Finalize();
    return 0;
}