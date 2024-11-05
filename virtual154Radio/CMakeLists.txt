cmake_minimum_required(VERSION 3.15)
project(virtual154Radio C)

add_subdirectory(include)
add_subdirectory(src)

set(TARGET_GROUP production CACHE STRING "Group to build")

#if(TARGET_GROUP STREQUAL test)
#    add_subdirectory(test)
#endif()

if (BUILD_EXAMPLES)
    message(STATUS "Building virtual154Radio exmaples")
    add_subdirectory(examples)
endif()
