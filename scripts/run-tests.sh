#!/bin/bash
set -e
cd "$(dirname "$0")/.."

mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DLUNDI_BUILD_TESTS=ON
make -j$(nproc)
echo "Build complete. Running tests"
./tests/lundi_tests
echo "Tests complete"

