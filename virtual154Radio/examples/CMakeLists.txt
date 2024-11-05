project(virtual154Radio)

add_executable(virtual154RadioReceiver virtual154RadioReceiver.c)
target_link_libraries(virtual154RadioReceiver
    PRIVATE virtual154Radio
    PRIVATE virtualLink
    PRIVATE lmac154Frame
)

add_executable(virtual154RadioSender virtual154RadioSender.c)
target_link_libraries(virtual154RadioSender
    PRIVATE virtual154Radio
    PRIVATE virtualLink
    PRIVATE lmac154Frame
)
