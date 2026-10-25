#!/bin/bash
set -e
echo "Running ASan/UBSan build for Nemo..."
cmake -B build_asan -DCMAKE_BUILD_TYPE=Debug -DNEMO_ENABLE_ASAN=ON -DNEMO_ENABLE_UBSAN=ON
cmake --build build_asan
./build_asan/tests/nemo_test_suite

echo "Running TSan build for Nemo..."
cmake -B build_tsan -DCMAKE_BUILD_TYPE=Debug -DNEMO_ENABLE_TSAN=ON
cmake --build build_tsan
./build_tsan/tests/nemo_test_suite
