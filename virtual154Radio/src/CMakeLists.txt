project(virtual154Radio)

add_library(virtual154Radio virtual154Radio.c)

target_include_directories(virtual154Radio
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)

target_link_libraries(virtual154Radio
    PRIVATE logger
    PRIVATE virtualLink
    PRIVATE lmac154Frame
    PRIVATE systemTime)
