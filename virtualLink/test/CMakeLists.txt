add_executable(virtualLinkTest virtualLinkTest.c)

target_link_libraries(virtualLinkTest
    PRIVATE dumbFuzzer
    PRIVATE virtualLink
    PRIVATE unity)

add_test(NAME virtualLinkTest COMMAND virtualLinkTest)
