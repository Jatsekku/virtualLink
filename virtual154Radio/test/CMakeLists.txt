add_executable(virtual154RadioTest virtual154RadioTest.c)

target_link_libraries(virtual154RadioTest
    PRIVATE dumbFuzzer
    PRIVATE virtualLink
    PRIVATE virtual154Radio
    PRIVATE unity)

add_test(NAME virtualLinkTest COMMAND virtualLinkTest)
