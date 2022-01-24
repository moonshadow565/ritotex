cmake_minimum_required(VERSION 3.20)

project(ritotex)

add_subdirectory(detex)

add_executable(ritotex src/main.c)
target_include_directories(ritotex PRIVATE src/)
target_link_libraries(ritotex PRIVATE detex)