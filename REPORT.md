# REPORT

## Project Goal

The goal of this project is to run two classic distributed graph algorithms with MPI on a graph whose nodes are partitioned across a small number of processes. Instead of forcing one graph node to equal one MPI rank, each rank owns a subset of graph nodes. This better matches the project requirement and better represents how distributed systems usually handle large logical graphs.

The current implementation supports:

- distributed leader election
- distributed Dijkstra shortest paths
- file-based graph input
- file-based partition input
- saved metrics for each run
- runnable integration tests

## Approach And Overall Idea

My overall approach was to first make the MPI runtime correct on small repeatable graphs before adding more automation. I started with a simple graph representation and a simple partition model so that I could verify both algorithms end to end with known expected answers.

The main idea is:

1. load a weighted undirected graph from a text file
2. load a partition file that maps each node to an owner rank
3. let each rank build its owned-node set and ghost-node set
4. run distributed leader election or distributed Dijkstra using MPI collectives
5. record iterations, runtime, approximate message count, and approximate bytes sent

This approach favors correctness, readability, and reproducibility on small experiments. Once that foundation works, the same structure can be extended to larger graphs and more automated generation.

## Design

The graph is stored as an adjacency map from node id to a vector of weighted edges. The partition file maps each node id to exactly one MPI rank. After loading input, each process computes:

- `owned_nodes`: nodes assigned to that rank
- `ghost_nodes`: neighboring nodes whose owner rank is different

This split is important because communication only becomes necessary when edges cross rank boundaries. Even though the current implementation uses collectives as a simple baseline, the ownership model is already structured around partitioned graph execution.

The MPI runtime is implemented in `mpi_runtime/src/main.cpp`. It accepts:

- `--graph <path>`
- `--part <path>`
- `--algo leader`
- `--algo dijkstra --source <node>`

The runtime also validates common input errors such as:

- missing graph or partition files
- invalid node ids
- invalid source node
- partition owners outside the MPI world size

## Implementation Details

### Graph And Partition Input

The graph file format is:

```text
<number_of_nodes>
<u> <v> <weight>
<u> <v> <weight>
...
```

The partition file format is:

```text
<node_id> <owner_rank>
<node_id> <owner_rank>
...
```

During loading, the runtime creates an undirected adjacency structure by inserting both `u -> v` and `v -> u` for each edge. Positive weights are required because Dijkstra assumes nonnegative edge costs.

### Leader Election Implementation

Leader election uses a FloodMax-style algorithm. Each owned node starts with its own node id as its current leader candidate. In every round:

1. local candidate information is propagated across same-rank neighbors
2. each rank computes its current local maximum candidate
3. `MPI_Allgather` shares one rank-level candidate from every process
4. each owned node updates toward the largest candidate seen

The implementation uses `total_nodes` rounds as a safe upper bound for these small connected graphs. At the end, the program checks agreement with `MPI_Allreduce` and reports success only if all owned nodes across all ranks agree on the same final leader.

### Dijkstra Implementation

Dijkstra is implemented as a distributed baseline with global minimum selection. Each rank keeps:

- a distance array for all nodes
- a visited array
- ownership information to know which rank should relax a selected node

Each iteration works like this:

1. every rank finds its best local unsettled owned node
2. `MPI_Allgather` collects all local best proposals
3. the globally best node is selected
4. the owner rank relaxes edges from that node
5. `MPI_Allreduce` with `MPI_MIN` shares the best proposed distance updates

This design is not the most optimized possible MPI shortest-path algorithm, but it is easy to reason about and produces correct results on the tested graphs.

### Metrics And Output Files

For each run, rank 0 records:

- algorithm name
- graph file
- partition file
- source node for Dijkstra
- iteration count
- runtime in seconds
- approximate message count
- approximate bytes sent

These are written into `outputs/` as files such as:

- `leader_graph_metrics.txt`
- `dijkstra_graph_metrics.txt`
- `leader_graph2_metrics.txt`
- `dijkstra_graph2_metrics.txt`

### Testing

The repository includes `experiments/run_tests.sh`, which rebuilds the project and runs five integration checks:

1. leader election on the first graph
2. Dijkstra on the first graph
3. leader election on the second graph
4. Dijkstra on the second graph
5. invalid source rejection

This gave me a simple way to check correctness after code changes without rerunning everything manually.

## Algorithm Choices

I chose FloodMax-style leader election because it is simple, deterministic, and appropriate for a connected graph with unique node ids. It also maps naturally to MPI collectives for a small project baseline.

I chose a collective-based distributed Dijkstra because it is easier to implement correctly than a more asynchronous approach. The `MPI_Allgather` plus `MPI_Allreduce` design makes the global minimum selection explicit, which is helpful for debugging and for explaining the distributed coordination cost.

These choices prioritize correctness and clarity over maximum performance. For a class project, that tradeoff makes sense because it creates a working end-to-end baseline that can be measured and extended.

## Experimental Hypothesis

Before running the experiments, my hypothesis was:

- leader election should converge to the maximum node id in each connected graph
- Dijkstra should produce the hand-computed shortest distances from source node `0`
- the larger five-node graph should require more iterations and more communication than the four-node chain graph

I also expected Dijkstra to show noticeably higher communication cost than leader election because it performs repeated global minimum selection and distributed relaxation updates.

## Expected Results

For `graph.txt`, I expected:

- elected leader: `3`
- shortest distances from source `0`: `0, 1, 3, 6`

For `graph2.txt`, I expected:

- elected leader: `4`
- shortest distances from source `0`: `0, 3, 1, 4, 7`

I also expected the five-node graph to have higher iteration counts and message counts than the four-node graph.

## Actual Results With Explanation

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

Actual results:

- leader election converged to leader `3`
- Dijkstra from source `0` produced:
  `0 -> 0`, `1 -> 1`, `2 -> 3`, `3 -> 6`
- leader metrics from `outputs/leader_graph_metrics.txt`:
  `Iterations: 4`, `Runtime: 8.7e-05`, `Messages: 10`, `Bytes: 20`
- Dijkstra metrics from `outputs/dijkstra_graph_metrics.txt`:
  `Iterations: 4`, `Runtime: 0.001765`, `Messages: 24`, `Bytes: 96`

Explanation:

These results match the expected answers exactly. The chain graph has only one simple path between any two nodes, so the shortest-path values are easy to verify by hand. The leader result is also straightforward because the maximum node id is `3`. Dijkstra used more communication than leader election, which matches the hypothesis.

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

Actual results:

- leader election converged to leader `4`
- Dijkstra from source `0` produced:
  `0 -> 0`, `1 -> 3`, `2 -> 1`, `3 -> 4`, `4 -> 7`
- leader metrics from `outputs/leader_graph2_metrics.txt`:
  `Iterations: 5`, `Runtime: 9.3e-05`, `Messages: 12`, `Bytes: 24`
- Dijkstra metrics from `outputs/dijkstra_graph2_metrics.txt`:
  `Iterations: 5`, `Runtime: 0.002558`, `Messages: 30`, `Bytes: 140`

Explanation:

These results also match the expected values. The shortest path to node `1` is not the direct edge of weight `4`; instead it is `0 -> 2 -> 1` with total cost `3`, which confirms that the relaxation logic is working correctly. As expected, this larger graph required more iterations and more communication than the first graph.

## Specific Decisions And Insights

- I intentionally used small text-based graph files first instead of immediately integrating a generator. This made debugging MPI behavior much easier.
- I used a partitioned ownership model early, even though the algorithms are still simple, because that is the key design shift in the assignment.
- I chose collectives for both algorithms because they are easier to implement correctly and reason about in a course project setting.
- Saving metrics to files in `outputs/` made it much easier to write the report and preserve experiment results.
- Adding a small integration test script helped guard against regressions while updating the runtime and documentation.

## Reproducibility

The project uses fixed graph and partition files under `outputs/`, so each documented command is reproducible. Re-running the same command recreates both the terminal output and the corresponding metrics summary file for that run.

## Assumptions And Limitations

- graphs must be connected
- edge weights must be positive
- node ids must be in the range `0` to `N - 1`
- every node must appear exactly once in the partition file
- the current input format is a simple text format rather than full NetGameSim export
- metrics are approximate communication summaries based on collective usage
- the current implementation is designed as a correct baseline for small graphs

## Next Steps

- integrate graph generation or graph import from a NetGameSim-style workflow
- add a small partition generator tool
- improve structured logging and metrics detail
- add more graph cases and partition strategies for deeper experiments
