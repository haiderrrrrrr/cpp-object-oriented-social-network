cmake_minimum_required(VERSION 3.16)

project(object_oriented_social_network LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(
    social-network
    src/main.cpp
)

if(MSVC)
    target_compile_options(social-network PRIVATE /W4)
else()
    target_compile_options(social-network PRIVATE -Wall -Wextra -Wpedantic)
endif()
