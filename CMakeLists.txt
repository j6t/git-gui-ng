cmake_minimum_required(VERSION 3.1)
project(GitGUI18)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(git-guing_SRCS
	git-gui.cpp
)

add_executable(git-guing ${git-guing_SRCS})
