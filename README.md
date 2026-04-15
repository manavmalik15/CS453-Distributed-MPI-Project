# NetGameSim to MPI Distributed Algorithms Project

This repository contains a small MPI runtime for running distributed leader election and distributed Dijkstra shortest paths on a graph partitioned across MPI ranks. The current version uses simple text-based graph and partition inputs stored in `outputs/`.

## Dependencies

- C++17 compiler
- MPI C++ wrapper such as OpenMPI with `mpicxx` and `mpirun`
- CMake 3.16 or newer

## Build

From the project root:

```bash
cmake -S mpi_runtime -B build
cmake --build build
```

This produces the executable:

```text
build/ngs_mpi
```

## Run

Leader election on the first test graph:

```bash
mpirun -n 2 ./build/ngs_mpi --graph outputs/graph.txt --part outputs/part.txt --algo leader
```

Distributed Dijkstra from source node `0` on the first test graph:

```bash
mpirun -n 2 ./build/ngs_mpi --graph outputs/graph.txt --part outputs/part.txt --algo dijkstra --source 0
```

Leader election on the second test graph:

```bash
mpirun -n 2 ./build/ngs_mpi --graph outputs/graph2.txt --part outputs/part2.txt --algo leader
```

Distributed Dijkstra on the second test graph:

```bash
mpirun -n 2 ./build/ngs_mpi --graph outputs/graph2.txt --part outputs/part2.txt --algo dijkstra --source 0
```

## Input Formats

Graph file format:

```text
<number_of_nodes>
<u> <v> <weight>
<u> <v> <weight>
...
```

Example from `outputs/graph.txt`:

```text
4
0 1 1
1 2 2
2 3 3
```

Partition file format:

```text
<node_id> <owner_rank>
<node_id> <owner_rank>
...
```

Example from `outputs/part.txt`:

```text
0 0
1 0
2 1
3 1
```

## Outputs

Each run prints algorithm results to the terminal and writes a metrics summary file into `outputs/`.

Examples:

- `outputs/leader_graph_metrics.txt`
- `outputs/dijkstra_graph_metrics.txt`
- `outputs/leader_graph2_metrics.txt`
- `outputs/dijkstra_graph2_metrics.txt`

Each metrics file includes:

- algorithm
- graph file
- partition file
- source node for Dijkstra
- iterations
- runtime
- approximate message count
- approximate bytes sent

## Assumptions

- Node ids are integers in the range `0` to `N - 1`
- Graphs are undirected
- Edge weights are positive
- Graphs are connected
- Every node appears exactly once in the partition file
- Owner ranks in the partition file must be valid for the chosen MPI world size

## Current Test Graphs

The repository currently includes two small test cases:

- `outputs/graph.txt` with `outputs/part.txt`
- `outputs/graph2.txt` with `outputs/part2.txt`

These provide quick reproducible runs for both leader election and shortest paths.

## Tests

Run the integration test suite with:

```bash
bash experiments/run_tests.sh
```

The script rebuilds the MPI runtime and runs 5 checks:

- leader election on `graph.txt`
- Dijkstra on `graph.txt`
- leader election on `graph2.txt`
- Dijkstra on `graph2.txt`
- invalid source input validation
