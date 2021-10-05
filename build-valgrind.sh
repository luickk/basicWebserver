#!/usr/bin/env bash
mkdir build_val
cd build_val
cmake ..
make
valgrind --tool=memcheck -s --track-origins=yes ./basicWebserver
