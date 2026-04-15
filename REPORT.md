# REPORT

## Overview

This project implements a simple MPI-based runtime for distributed graph algorithms on a partitioned graph. The graph is loaded from a text file, ownership is loaded from a partition file, and each MPI rank stores the nodes it owns plus ghost-node information for neighbors owned by other ranks. The current implementation supports:

- distributed leader election
- distributed Dijkstra shortest paths
- file-based graph and partition input
- per-run metrics written to `outputs/`

## Design

The graph is represented as an adjacency map from node id to a vector of weighted edges. The partition file maps each node id to exactly one owner rank. At startup, each rank computes:

- `owned_nodes`: nodes assigned to that rank
- `ghost_nodes`: neighboring nodes owned by a different rank

This design follows the project requirement that graph nodes are logical entities and are not forced to map one-to-one with MPI processes.

## Leader Election Approach

Leader election uses a FloodMax-style approach. Each owned node starts with its own node id as its current leader candidate. In each round:

1. local neighbor information is propagated for neighbors on the same rank
2. each rank contributes its current local maximum candidate using `MPI_Allgather`
3. each owned node updates its candidate using the maximum value observed

After enough rounds, all nodes converge to the maximum node id in the connected graph. For the current test graphs, the elected leaders are:

- `3` for `graph.txt`
- `4` for `graph2.txt`

## Dijkstra Approach

Dijkstra uses a simple distributed baseline with global minimum selection. Each rank tracks tentative distances for all nodes and chooses its best local unsettled owned node. The ranks then use collectives to determine the single global best node for the next relaxation step.

Per iteration:

1. each rank proposes its best local unsettled node
2. `MPI_Allgather` is used to compare local proposals
3. the global best node is selected
4. the owner rank relaxes outgoing edges from that node
5. `MPI_Allreduce` with `MPI_MIN` shares the best proposed updates

This is not a fully asynchronous shortest-path algorithm, but it is a correct MPI baseline for positive-weight graphs and clearly demonstrates the distributed coordination cost.

## Experiments

### Experiment 1: Chain Graph

Files used:

- `outputs/graph.txt`
- `outputs/part.txt`

Graph:

```text
4
0 1 1
1 2 2
2 3 3
```

Partition:

```text
0 0
1 0
2 1
3 1
```

Results:

- leader election converged to leader `3`
- Dijkstra from source `0` produced distances:
  `0 -> 0`, `1 -> 1`, `2 -> 3`, `3 -> 6`
- leader metrics from `outputs/leader_graph_metrics.txt`:
  `Iterations: 4`, `Runtime: 8.7e-05`, `Messages: 10`, `Bytes: 20`
- Dijkstra metrics from `outputs/dijkstra_graph_metrics.txt`:
  `Iterations: 4`, `Runtime: 0.001765`, `Messages: 24`, `Bytes: 96`

### Experiment 2: Five-Node Weighted Graph

Files used:

- `outputs/graph2.txt`
- `outputs/part2.txt`

Graph:

```text
5
0 1 4
0 2 1
2 1 2
1 3 1
2 3 5
3 4 3
```

Partition:

```text
0 0
1 0
2 1
3 1
4 1
```

Results:

- leader election converged to leader `4`
- Dijkstra from source `0` produced distances:
  `0 -> 0`, `1 -> 3`, `2 -> 1`, `3 -> 4`, `4 -> 7`
- leader metrics from `outputs/leader_graph2_metrics.txt`:
  `Iterations: 5`, `Runtime: 9.3e-05`, `Messages: 12`, `Bytes: 24`
- Dijkstra metrics from `outputs/dijkstra_graph2_metrics.txt`:
  `Iterations: 5`, `Runtime: 0.002558`, `Messages: 30`, `Bytes: 140`

## Reproducibility

The project currently uses text files under `outputs/` for repeatable test cases. Re-running the same command with the same graph and partition files reproduces the same logical results and generates a matching metrics summary file for that run.

## Testing

The repository includes a runnable integration test script at `experiments/run_tests.sh`. It rebuilds the runtime and executes 5 checks covering:

- leader election correctness on the first graph
- Dijkstra correctness on the first graph
- leader election correctness on the second graph
- Dijkstra correctness on the second graph
- input validation for an invalid Dijkstra source node

## Assumptions and Limitations

- Graphs must be connected
- Edge weights must be positive
- The current input format is a simple text format rather than full NetGameSim export
- Metrics are approximate communication summaries based on collective usage
- The current implementation focuses on correctness and reproducibility for small graphs

## Next Steps

- integrate graph generation with NetGameSim exports
- add a partition generator tool
- improve metrics detail and logging
- add automated tests
- expand experiments to compare graph structures or partition strategies
