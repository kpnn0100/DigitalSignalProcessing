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
echo "About to build"
for file in "${SOURCE_FILES[@]}"; do
  echo "$file"
done

# Build the test executable
$CXX_COMPILER $COMPILE_FLAGS -Wall -g -pthread "${SOURCE_FILES[@]}" $LINKER_FLAGS -o unit_tests
# Run the tests
./unit_tests

# Clean up the test executable
rm unit_tests
