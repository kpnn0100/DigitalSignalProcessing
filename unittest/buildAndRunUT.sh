#!/bin/bash

# Define the compiler and compile flags
CXX_COMPILER=g++
COMPILE_FLAGS="-std=c++11"
LINKER_FLAGS="-lgtest -lgtest_main -lpthread"

# Define the source and test files
SOURCE_FILES=(
  ../**/*.cpp          # Include all .cpp files in the src directory
)
SOURCE_FILES=(${SOURCE_FILES[@]/src\/main\.cpp}) # Exclude main.cpp

TEST_FILES=(
  *.cpp
  # Add more test files as needed
)

# Build the test executable
$CXX_COMPILER $COMPILE_FLAGS "${SOURCE_FILES[@]}" "${TEST_FILES[@]}" $LINKER_FLAGS -o unit_tests

# Run the tests
./unit_tests

# Clean up the test executable
rm unit_tests
