cmake_minimum_required(VERSION 3.19)

set(CMAKE_CXX_STANDARD 20)

project(ThreadPool)

add_executable(ThreadPool ThreadPool.cpp main.cpp)