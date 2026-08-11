find_package(Threads REQUIRED)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(RDPLIB_SOURCE_FAITHFUL)
    add_compile_definitions(RDPLIB_SOURCE_FAITHFUL RDPLIB_TEST_SOURCE_FAITHFUL)
endif()

add_executable(rdplib_end_to_end end_to_end.c)
target_link_libraries(rdplib_end_to_end PRIVATE rdplib Threads::Threads)
add_test(NAME rdplib_end_to_end COMMAND rdplib_end_to_end)

add_executable(rdplib_api api.c)
target_link_libraries(rdplib_api PRIVATE rdplib Threads::Threads)
add_test(NAME rdplib_api COMMAND rdplib_api)

if(RDPLIB_BUILD_SHARED)
    add_executable(rdplib_shared_api api.c)
    target_link_libraries(rdplib_shared_api PRIVATE rdplib_shared Threads::Threads)
    add_test(NAME rdplib_shared_api COMMAND rdplib_shared_api)
    set_tests_properties(rdplib_shared_api PROPERTIES WORKING_DIRECTORY "$<TARGET_FILE_DIR:rdplib_shared>")
endif()

add_executable(rdplib_tx_ack tx_ack.c)
target_link_libraries(rdplib_tx_ack PRIVATE rdplib)
add_test(NAME rdplib_tx_ack COMMAND rdplib_tx_ack)

add_executable(rdplib_connection_core connection_core.c)
target_link_libraries(rdplib_connection_core PRIVATE rdplib)
add_test(NAME rdplib_connection_core COMMAND rdplib_connection_core)

add_executable(rdplib_connhash_core connhash_core.c)
target_link_libraries(rdplib_connhash_core PRIVATE rdplib)
add_test(NAME rdplib_connhash_core COMMAND rdplib_connhash_core)

add_executable(rdplib_tx_retry tx_retry.c)
target_link_libraries(rdplib_tx_retry PRIVATE rdplib)
add_test(NAME rdplib_tx_retry COMMAND rdplib_tx_retry)

add_executable(rdplib_tx_history_capacity tx_history_capacity.c)
target_link_libraries(rdplib_tx_history_capacity PRIVATE rdplib)
add_test(NAME rdplib_tx_history_capacity COMMAND rdplib_tx_history_capacity)

add_executable(rdplib_tx_ack_preflight tx_ack_preflight.c)
target_link_libraries(rdplib_tx_ack_preflight PRIVATE rdplib)
add_test(NAME rdplib_tx_ack_preflight COMMAND rdplib_tx_ack_preflight)

add_executable(rdplib_boundary_regression boundary_regression.c)
target_link_libraries(rdplib_boundary_regression PRIVATE rdplib)

if(NOT RDPLIB_SOURCE_FAITHFUL)
    add_test(NAME rdplib_usend_boundary COMMAND rdplib_boundary_regression usend-overflow)
    add_test(NAME rdplib_parser_boundary COMMAND rdplib_boundary_regression parser-short)
    add_test(NAME rdplib_fragment_final_large COMMAND rdplib_boundary_regression fragment-final-large)
    add_test(NAME rdplib_fragment_final_zero COMMAND rdplib_boundary_regression fragment-final-zero)
    add_test(NAME rdplib_stream_boundary COMMAND rdplib_boundary_regression send-stream)
    add_test(NAME rdplib_history_boundary COMMAND rdplib_boundary_regression send-history)
    add_test(NAME rdplib_connhash_boundary COMMAND rdplib_boundary_regression connhash-invalid)

    add_executable(rdplib_random_isolation random_isolation.c)
    target_link_libraries(rdplib_random_isolation PRIVATE rdplib)
    add_test(NAME rdplib_random_isolation COMMAND rdplib_random_isolation)

    add_executable(rdplib_pre_syn_unreliable pre_syn_unreliable.c)
    target_link_libraries(rdplib_pre_syn_unreliable PRIVATE rdplib Threads::Threads)
    add_test(NAME rdplib_pre_syn_unreliable COMMAND rdplib_pre_syn_unreliable)
    set_tests_properties(rdplib_pre_syn_unreliable PROPERTIES TIMEOUT 15)

    add_executable(rdplib_icmp_dispatch icmp_dispatch.c)
    target_link_libraries(rdplib_icmp_dispatch PRIVATE rdplib)
    add_test(NAME rdplib_icmp_dispatch COMMAND rdplib_icmp_dispatch)
    set_tests_properties(rdplib_icmp_dispatch PROPERTIES TIMEOUT 10)

    if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(rdplib_icmp_ingress icmp_ingress.c)
        target_link_libraries(rdplib_icmp_ingress PRIVATE rdplib)
        add_test(NAME rdplib_icmp_ingress COMMAND rdplib_icmp_ingress)
        set_tests_properties(rdplib_icmp_ingress PROPERTIES TIMEOUT 10)
    endif()

    if(WIN32)
        add_executable(rdplib_windows_udp_connreset windows_udp_connreset.c)
        target_link_libraries(rdplib_windows_udp_connreset PRIVATE rdplib)
        add_test(NAME rdplib_windows_udp_connreset COMMAND rdplib_windows_udp_connreset)
        set_tests_properties(rdplib_windows_udp_connreset PROPERTIES TIMEOUT 10)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(rdplib_linux_ip_recverr linux_ip_recverr.c)
        target_link_libraries(rdplib_linux_ip_recverr PRIVATE rdplib)
        add_test(NAME rdplib_linux_ip_recverr COMMAND rdplib_linux_ip_recverr)
        set_tests_properties(rdplib_linux_ip_recverr PROPERTIES TIMEOUT 10)
    endif()
endif()
