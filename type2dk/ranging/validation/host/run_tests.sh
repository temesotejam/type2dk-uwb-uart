#!/usr/bin/env bash
set -eu
cd "$(dirname "$0")"
test_out=$(mktemp -d)
trap 'rm -rf "$test_out"' EXIT
for node in 19 21 22; do
  gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-parameter -fsanitize=address,undefined -g -DMESH_NODE="$node" -I. -I../../../../include test_recovery.c -o "$test_out/recovery$node"
  ASAN_OPTIONS=detect_leaks=0 "$test_out/recovery$node"
done
