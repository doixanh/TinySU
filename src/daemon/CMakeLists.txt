cmake_minimum_required(VERSION 3.6)
project(daemon)

set(CMAKE_CXX_STANDARD 11)

set(SOURCE_FILES
        tinysu.c
        tinysu.h)

add_executable(daemon ${SOURCE_FILES})