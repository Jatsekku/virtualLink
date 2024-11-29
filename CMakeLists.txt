cmake_minimum_required(VERSION 3.15)
project(virtualLink C)

add_subdirectory(include)
add_subdirectory(src)

if(TARGET_GROUP STREQUAL test)
    add_subdirectory(test)
endif()
