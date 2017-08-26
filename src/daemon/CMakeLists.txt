cmake_minimum_required(VERSION 3.6)
project(daemon)

set(CMAKE_CXX_STANDARD 11)
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES
        tinysu.cpp
        tinysu.h daemon.cpp daemon.h client.cpp client.h)

add_executable(daemon ${SOURCE_FILES})