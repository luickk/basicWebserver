#!/usr/bin/env bash
mkdir build
cd build
cmake ..
make
cd ..
exec build/basicWebserver
# valgrind --tool=memcheck -s --track-origins=yes ./build/basicWebserver
#leaks -atExit -- ./build/basicWebserver
