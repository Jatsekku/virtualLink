project(virtualLink)

add_library(virtualLink virtualLink.c)

target_include_directories(virtualLink
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)

target_link_libraries(virtualLink
    PRIVATE logger
    PRIVATE systemTime
)
