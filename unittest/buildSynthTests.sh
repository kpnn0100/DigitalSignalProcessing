#!/bin/bash
# Builds and runs the dependency-free synth unit tests (no gtest/libsndfile).
# Sources live under src/. Excluded: spatial/ (legacy mono process()) and its
# util/ support (Coordinate has a pre-existing bug) — neither is on the synth path.
set -e
cd "$(dirname "$0")/.."   # DigitalSignalProcessing/

SRC=$(find src -name '*.cpp' -not -path 'src/spatial/*' -not -path 'src/util/*')

g++ -std=c++17 -O2 -pthread unittest/synthTests.cpp unittest/coverageTests.cpp unittest/filterTests.cpp unittest/instrumentTests.cpp $SRC -o /tmp/synthTests
/tmp/synthTests
