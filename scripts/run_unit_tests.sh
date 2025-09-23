#!/bin/bash

# This file is made to support the unit tests workflow.
# It should only require the directories build/tests and scripts/ to function,
# and cmake (with ctest) installed.
# (otherwise, update the workflow too, but try not to)

SCRIPT_DIR="$(dirname "$0")"

pushd "${SCRIPT_DIR}/../build/tests" || exit 1

# TODO: github.com/SoftFever/OrcaSlicer/issues/10309 - Run all tests
ctest --test-dir slic3rutils --output-junit ctest_results.xml
