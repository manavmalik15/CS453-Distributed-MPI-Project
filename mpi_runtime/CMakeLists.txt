cmake_minimum_required(VERSION 3.16)

project(ngs_mpi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(MPI REQUIRED)

add_executable(ngs_mpi
    src/main.cpp
)

target_include_directories(ngs_mpi
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ngs_mpi
    PRIVATE
        MPI::MPI_CXX
)
