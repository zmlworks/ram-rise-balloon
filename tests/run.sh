#!/bin/sh
# Unit tests for balloon trigger judgement (runs on Linux/macOS with a C compiler).
set -eu
cd "$(dirname "$0")/.."
mkdir -p tests
cc -Wall -Wextra -Werror -O2 -o tests/test_judge tests/test_judge.c -lm
./tests/test_judge
