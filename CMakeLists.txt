cmake_minimum_required(VERSION 3.10)

project(basicWebserver)

SET(GCC_COVERAGE_COMPILE_FLAGS "-g")

add_executable(basicWebserver webserver.c)
