#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
BINARY="$BUILD_DIR/ngs_mpi"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

pass_count=0

run_test() {
    local name="$1"
    shift

    if "$@"; then
        echo "[PASS] $name"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] $name"
        exit 1
    fi
}

assert_contains() {
    local file="$1"
    local pattern="$2"
    grep -Fq "$pattern" "$file"
}

build_binary() {
    cmake -S "$ROOT_DIR/mpi_runtime" -B "$BUILD_DIR" >/dev/null
    cmake --build "$BUILD_DIR" >/dev/null
}

test_leader_graph1() {
    local out_file="$TMP_DIR/leader_graph1.txt"
    mpirun -n 2 "$BINARY" --graph "$ROOT_DIR/outputs/graph.txt" --part "$ROOT_DIR/outputs/part.txt" --algo leader >"$out_file"
    assert_contains "$out_file" "Leader election successful: all nodes agree on leader 3"
    assert_contains "$out_file" "Node 0 -> Leader 3"
    assert_contains "$out_file" "Node 3 -> Leader 3"
}

test_dijkstra_graph1() {
    local out_file="$TMP_DIR/dijkstra_graph1.txt"
    mpirun -n 2 "$BINARY" --graph "$ROOT_DIR/outputs/graph.txt" --part "$ROOT_DIR/outputs/part.txt" --algo dijkstra --source 0 >"$out_file"
    assert_contains "$out_file" "Node 0 -> 0"
    assert_contains "$out_file" "Node 1 -> 1"
    assert_contains "$out_file" "Node 2 -> 3"
    assert_contains "$out_file" "Node 3 -> 6"
}

test_leader_graph2() {
    local out_file="$TMP_DIR/leader_graph2.txt"
    mpirun -n 2 "$BINARY" --graph "$ROOT_DIR/outputs/graph2.txt" --part "$ROOT_DIR/outputs/part2.txt" --algo leader >"$out_file"
    assert_contains "$out_file" "Leader election successful: all nodes agree on leader 4"
    assert_contains "$out_file" "Node 0 -> Leader 4"
    assert_contains "$out_file" "Node 4 -> Leader 4"
}

test_dijkstra_graph2() {
    local out_file="$TMP_DIR/dijkstra_graph2.txt"
    mpirun -n 2 "$BINARY" --graph "$ROOT_DIR/outputs/graph2.txt" --part "$ROOT_DIR/outputs/part2.txt" --algo dijkstra --source 0 >"$out_file"
    assert_contains "$out_file" "Node 0 -> 0"
    assert_contains "$out_file" "Node 1 -> 3"
    assert_contains "$out_file" "Node 2 -> 1"
    assert_contains "$out_file" "Node 3 -> 4"
    assert_contains "$out_file" "Node 4 -> 7"
}

test_invalid_source() {
    local out_file="$TMP_DIR/invalid_source.txt"
    if mpirun -n 2 "$BINARY" --graph "$ROOT_DIR/outputs/graph.txt" --part "$ROOT_DIR/outputs/part.txt" --algo dijkstra --source 99 >"$out_file" 2>&1; then
        return 1
    fi
    assert_contains "$out_file" "Invalid source node: 99"
}

build_binary

run_test "Leader election on graph.txt" test_leader_graph1
run_test "Dijkstra on graph.txt" test_dijkstra_graph1
run_test "Leader election on graph2.txt" test_leader_graph2
run_test "Dijkstra on graph2.txt" test_dijkstra_graph2
run_test "Invalid source is rejected" test_invalid_source

echo "All $pass_count integration tests passed."
