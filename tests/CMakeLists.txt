find_package(Threads REQUIRED)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(RDPLIB_SOURCE_FAITHFUL)
    add_compile_definitions(RDPLIB_SOURCE_FAITHFUL RDPLIB_TEST_SOURCE_FAITHFUL)
endif()

add_executable(rdplib_platform_sync_core platform_sync_core.c)
target_link_libraries(rdplib_platform_sync_core PRIVATE rdplib)
add_test(NAME rdplib_platform_sync_core COMMAND rdplib_platform_sync_core)
set_tests_properties(rdplib_platform_sync_core PROPERTIES TIMEOUT 10)

add_executable(rdplib_bandwidth_core bandwidth_core.c ../rdplib/src/bandwidth.c)
target_include_directories(rdplib_bandwidth_core PRIVATE ../rdplib/src)
add_test(NAME rdplib_bandwidth_core COMMAND rdplib_bandwidth_core)

add_executable(rdplib_trace_core trace_core.c ../rdplib/src/trace.c)
target_include_directories(rdplib_trace_core PRIVATE ../rdplib/src)
target_compile_definitions(rdplib_trace_core PRIVATE RDP_DEAD_CODE gethostbyaddr=trace_test_gethostbyaddr inet_ntoa=trace_test_inet_ntoa)
if(WIN32)
    target_compile_definitions(rdplib_trace_core PRIVATE WINSOCK_API_LINKAGE=)
    target_link_libraries(rdplib_trace_core PRIVATE ws2_32)
endif()
add_test(NAME rdplib_trace_core COMMAND rdplib_trace_core)
set_tests_properties(rdplib_trace_core PROPERTIES TIMEOUT 10)

if(UNIX)
    add_executable(rdplib_uevent_failure_core uevent_failure_core.c ../rdplib/platform/posix/uevent.c)
    target_include_directories(rdplib_uevent_failure_core PRIVATE ../rdplib/src ../rdplib/platform/posix)
    target_compile_definitions(rdplib_uevent_failure_core PRIVATE
        sem_init=uevent_test_sem_init
        sem_destroy=uevent_test_sem_destroy
        sem_post=uevent_test_sem_post
        sem_wait=uevent_test_sem_wait
    )
    add_test(NAME rdplib_uevent_failure_core COMMAND rdplib_uevent_failure_core)
    set_tests_properties(rdplib_uevent_failure_core PROPERTIES TIMEOUT 10)
endif()

add_executable(rdplib_profile_guards_core profile_guards_core.c)
target_link_libraries(rdplib_profile_guards_core PRIVATE rdplib)
add_test(NAME rdplib_profile_guards_core COMMAND rdplib_profile_guards_core)

add_executable(rdplib_dpf_core dpf_core.c)
target_link_libraries(rdplib_dpf_core PRIVATE rdplib)
add_test(NAME rdplib_dpf_core COMMAND rdplib_dpf_core)

add_executable(rdplib_end_to_end end_to_end.c)
target_link_libraries(rdplib_end_to_end PRIVATE rdplib Threads::Threads)
add_test(NAME rdplib_end_to_end COMMAND rdplib_end_to_end)

add_executable(rdplib_api api.c)
target_link_libraries(rdplib_api PRIVATE rdplib Threads::Threads)
add_test(NAME rdplib_api COMMAND rdplib_api)

add_executable(rdplib_multi_sender_api multi_sender_api.c)
target_link_libraries(rdplib_multi_sender_api PRIVATE rdplib Threads::Threads)
add_test(NAME rdplib_multi_sender_api COMMAND rdplib_multi_sender_api)
set_tests_properties(rdplib_multi_sender_api PROPERTIES TIMEOUT 30)

if(RDPLIB_BUILD_SHARED)
    add_executable(rdplib_shared_api api.c)
    target_link_libraries(rdplib_shared_api PRIVATE rdplib_shared Threads::Threads)
    add_test(NAME rdplib_shared_api COMMAND rdplib_shared_api)
    set_tests_properties(rdplib_shared_api PROPERTIES WORKING_DIRECTORY "$<TARGET_FILE_DIR:rdplib_shared>")

    add_executable(rdplib_shared_multi_sender_api multi_sender_api.c)
    target_link_libraries(rdplib_shared_multi_sender_api PRIVATE rdplib_shared Threads::Threads)
    add_test(NAME rdplib_shared_multi_sender_api COMMAND rdplib_shared_multi_sender_api)
    set_tests_properties(rdplib_shared_multi_sender_api PROPERTIES TIMEOUT 30 WORKING_DIRECTORY "$<TARGET_FILE_DIR:rdplib_shared>")
endif()

add_executable(rdplib_tx_ack tx_ack.c)
target_link_libraries(rdplib_tx_ack PRIVATE rdplib)
add_test(NAME rdplib_tx_ack COMMAND rdplib_tx_ack)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test_work/tx_ack")
set_tests_properties(rdplib_tx_ack PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test_work/tx_ack")

# Compile the recovered TX owner with private names for the six seams whose
# production owner objects are pulled together on ELF. Other seams keep their
# public names so linked helpers such as bandwidth share the test clock/state.
add_executable(rdplib_tx_core tx_core.c ../rdplib/src/tx.c)
target_include_directories(rdplib_tx_core PRIVATE ../rdplib/include ../rdplib/src)
target_compile_definitions(rdplib_tx_core PRIVATE
    rdplib_platform_malloc=tx_core_test_platform_malloc
    rdplib_platform_free=tx_core_test_platform_free
    rdplib_platform_socket_set_option=tx_core_test_socket_set_option
    rdplib_platform_last_socket_error=tx_core_test_last_socket_error
    rdplib_platform_send_datagram=tx_core_test_send_datagram
    connhash_lock=tx_core_test_connhash_lock
)
target_link_libraries(rdplib_tx_core PRIVATE rdplib)
if(WIN32)
    target_include_directories(rdplib_tx_core PRIVATE ../rdplib/platform/windows)
else()
    target_include_directories(rdplib_tx_core PRIVATE ../rdplib/platform/posix)
endif()
add_test(NAME rdplib_tx_core COMMAND rdplib_tx_core)
set_tests_properties(rdplib_tx_core PROPERTIES TIMEOUT 20)

add_executable(rdplib_connection_core connection_core.c)
target_link_libraries(rdplib_connection_core PRIVATE rdplib)
add_test(NAME rdplib_connection_core COMMAND rdplib_connection_core)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test_work/connection_core")
set_tests_properties(rdplib_connection_core PROPERTIES
    TIMEOUT 20
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test_work/connection_core"
)

add_executable(rdplib_rdp_core rdp_core.c)
target_link_libraries(rdplib_rdp_core PRIVATE rdplib)
add_test(NAME rdplib_rdp_core COMMAND rdplib_rdp_core)
set_tests_properties(rdplib_rdp_core PROPERTIES TIMEOUT 20)

# Compile the owner source with only its send to seam renamed. The production
# library supplies the remaining dependencies, while this copy owns rdp_wake
# so the deterministic mock cannot replace the platform backend globally.
add_executable(rdplib_rdp_wake_core rdp_wake_core.c ../rdplib/src/rdp.c)
target_include_directories(rdplib_rdp_wake_core PRIVATE ../rdplib/include ../rdplib/src)
target_compile_definitions(rdplib_rdp_wake_core PRIVATE
    rdplib_platform_send_datagram_to=rdp_wake_test_send_datagram_to
    usemaphore_decrement=rdp_wake_test_usemaphore_decrement
)
target_link_libraries(rdplib_rdp_wake_core PRIVATE rdplib)
if(WIN32)
    target_include_directories(rdplib_rdp_wake_core PRIVATE ../rdplib/platform/windows)
else()
    target_include_directories(rdplib_rdp_wake_core PRIVATE ../rdplib/platform/posix)
endif()
add_test(NAME rdplib_rdp_wake_core COMMAND rdplib_rdp_wake_core)
set_tests_properties(rdplib_rdp_wake_core PROPERTIES TIMEOUT 10)

add_executable(rdplib_connhash_core connhash_core.c)
target_link_libraries(rdplib_connhash_core PRIVATE rdplib)
add_test(NAME rdplib_connhash_core COMMAND rdplib_connhash_core)

if(NOT RDPLIB_SOURCE_FAITHFUL)
    add_executable(rdplib_connhash_alloc_failure_core connhash_alloc_failure_core.c)
    target_link_libraries(rdplib_connhash_alloc_failure_core PRIVATE rdplib)
    add_test(NAME rdplib_connhash_alloc_failure_core COMMAND rdplib_connhash_alloc_failure_core)
endif()

add_executable(rdplib_cmp_core cmp_core.c)
target_link_libraries(rdplib_cmp_core PRIVATE rdplib)
add_test(NAME rdplib_cmp_core COMMAND rdplib_cmp_core)

add_executable(rdplib_iov_core iov_core.c)
target_link_libraries(rdplib_iov_core PRIVATE rdplib)
add_test(NAME rdplib_iov_core COMMAND rdplib_iov_core)

add_executable(rdplib_protocol_limits_core protocol_limits_core.c)
target_link_libraries(rdplib_protocol_limits_core PRIVATE rdplib)
add_test(NAME rdplib_protocol_limits_core COMMAND rdplib_protocol_limits_core)

add_executable(rdplib_packet_core packet_core.c)
target_include_directories(rdplib_packet_core PRIVATE ../rdplib/src)
add_test(NAME rdplib_packet_core COMMAND rdplib_packet_core)

# list.c satisfies relocations in the recovered plain static list header bodies
# when a GNU debug build emits their otherwise uncalled copies.
add_executable(rdplib_dpc_core dpc_core.c ../rdplib/src/dpc.c ../rdplib/src/cmp.c ../rdplib/src/list.c)
target_include_directories(rdplib_dpc_core PRIVATE ../rdplib/src)
add_test(NAME rdplib_dpc_core COMMAND rdplib_dpc_core)

add_executable(rdplib_list_core list_core.c)
target_link_libraries(rdplib_list_core PRIVATE rdplib)
add_test(NAME rdplib_list_core COMMAND rdplib_list_core)

add_executable(rdplib_hash_core hash_core.c)
target_link_libraries(rdplib_hash_core PRIVATE rdplib)
add_test(NAME rdplib_hash_core COMMAND rdplib_hash_core)

add_executable(rdplib_hash_alloc_failure_core hash_alloc_failure_core.c)
target_include_directories(rdplib_hash_alloc_failure_core PRIVATE ../rdplib/src)
add_test(NAME rdplib_hash_alloc_failure_core COMMAND rdplib_hash_alloc_failure_core)

add_executable(rdplib_pqueue_core pqueue_core.c)
target_link_libraries(rdplib_pqueue_core PRIVATE rdplib)
add_test(NAME rdplib_pqueue_core COMMAND rdplib_pqueue_core)

# This standalone allocation seam test includes pqueue.c directly; list.c owns
# the external calls retained by its recovered plain static header copies.
add_executable(rdplib_pqueue_alloc_failure_core pqueue_alloc_failure_core.c ../rdplib/src/list.c)
target_include_directories(rdplib_pqueue_alloc_failure_core PRIVATE ../rdplib/src)
add_test(NAME rdplib_pqueue_alloc_failure_core COMMAND rdplib_pqueue_alloc_failure_core)

add_executable(rdplib_eventq_core eventq_core.c)
target_link_libraries(rdplib_eventq_core PRIVATE rdplib)
add_test(NAME rdplib_eventq_core COMMAND rdplib_eventq_core)

add_executable(rdplib_eventq_owner_core eventq_owner_core.c)
target_include_directories(rdplib_eventq_owner_core PRIVATE ../rdplib/src)
target_link_libraries(rdplib_eventq_owner_core PRIVATE rdplib)
add_test(NAME rdplib_eventq_owner_core COMMAND rdplib_eventq_owner_core)

add_executable(rdplib_umutex_core umutex_core.c)
target_link_libraries(rdplib_umutex_core PRIVATE rdplib)
add_test(NAME rdplib_umutex_core COMMAND rdplib_umutex_core)

add_executable(rdplib_fast_malloc_core fast_malloc_core.c)
target_link_libraries(rdplib_fast_malloc_core PRIVATE rdplib)
add_test(NAME rdplib_fast_malloc_core COMMAND rdplib_fast_malloc_core)

add_executable(rdplib_fast_malloc_owner_core fast_malloc_owner_core.c)
target_include_directories(rdplib_fast_malloc_owner_core PRIVATE ../rdplib/src)
target_link_libraries(rdplib_fast_malloc_owner_core PRIVATE rdplib)
add_test(NAME rdplib_fast_malloc_owner_core COMMAND rdplib_fast_malloc_owner_core)

add_executable(rdplib_rdpstat_core rdpstat_core.c)
target_link_libraries(rdplib_rdpstat_core PRIVATE rdplib)
add_test(NAME rdplib_rdpstat_core COMMAND rdplib_rdpstat_core)

add_executable(rdplib_log_core log_core.c)
target_link_libraries(rdplib_log_core PRIVATE rdplib)
add_test(NAME rdplib_log_core COMMAND rdplib_log_core)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test_work/log_core")
set_tests_properties(rdplib_log_core PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test_work/log_core")

add_executable(rdplib_msg_arrival_core msg_arrival_core.c)
target_link_libraries(rdplib_msg_arrival_core PRIVATE rdplib)
add_test(NAME rdplib_msg_arrival_core COMMAND rdplib_msg_arrival_core)

add_executable(rdplib_arrival_fragid_core arrival_fragid_core.c)
target_link_libraries(rdplib_arrival_fragid_core PRIVATE rdplib)
add_test(NAME rdplib_arrival_fragid_core COMMAND rdplib_arrival_fragid_core)

add_executable(rdplib_sequencer_core sequencer_core.c)
target_link_libraries(rdplib_sequencer_core PRIVATE rdplib)
add_test(NAME rdplib_sequencer_core COMMAND rdplib_sequencer_core)

add_executable(rdplib_rx_core rx_core.c)
target_link_libraries(rdplib_rx_core PRIVATE rdplib)
add_test(NAME rdplib_rx_core COMMAND rdplib_rx_core)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test_work/rx_core")
set_tests_properties(rdplib_rx_core PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test_work/rx_core")

if(NOT RDPLIB_SOURCE_FAITHFUL)
    # Compile the recovered owner separately so allocation failure can be
    # injected without replacing the allocator used by its dependencies.
    add_executable(rdplib_rx_alloc_failure_core rx_alloc_failure_core.c ../rdplib/src/rx.c)
    target_include_directories(rdplib_rx_alloc_failure_core PRIVATE ../rdplib/include ../rdplib/src)
    target_compile_definitions(rdplib_rx_alloc_failure_core PRIVATE fast_malloc=rx_test_fast_malloc)
    target_link_libraries(rdplib_rx_alloc_failure_core PRIVATE rdplib)
    if(WIN32)
        target_include_directories(rdplib_rx_alloc_failure_core PRIVATE ../rdplib/platform/windows)
    else()
        target_include_directories(rdplib_rx_alloc_failure_core PRIVATE ../rdplib/platform/posix)
    endif()
    add_test(NAME rdplib_rx_alloc_failure_core COMMAND rdplib_rx_alloc_failure_core)
endif()

add_executable(rdplib_msg_outgoing_core msg_outgoing_core.c)
target_link_libraries(rdplib_msg_outgoing_core PRIVATE rdplib)
add_test(NAME rdplib_msg_outgoing_core COMMAND rdplib_msg_outgoing_core)

add_executable(rdplib_rxq_core rxq_core.c)
target_link_libraries(rdplib_rxq_core PRIVATE rdplib)
add_test(NAME rdplib_rxq_core COMMAND rdplib_rxq_core)

add_executable(rdplib_txq_core txq_core.c)
target_link_libraries(rdplib_txq_core PRIVATE rdplib)
add_test(NAME rdplib_txq_core COMMAND rdplib_txq_core)

add_executable(rdplib_crc_core crc_core.c)
target_link_libraries(rdplib_crc_core PRIVATE rdplib)
add_test(NAME rdplib_crc_core COMMAND rdplib_crc_core)

add_executable(rdplib_cypher_core cypher_core.c)
target_link_libraries(rdplib_cypher_core PRIVATE rdplib)
add_test(NAME rdplib_cypher_core COMMAND rdplib_cypher_core)

add_executable(rdplib_bitops_core bitops_core.c)
target_link_libraries(rdplib_bitops_core PRIVATE rdplib)
add_test(NAME rdplib_bitops_core COMMAND rdplib_bitops_core)

add_executable(rdplib_timeout_core timeout_core.c)
target_link_libraries(rdplib_timeout_core PRIVATE rdplib)
add_test(NAME rdplib_timeout_core COMMAND rdplib_timeout_core)

add_executable(rdplib_utime_core utime_core.c)
target_link_libraries(rdplib_utime_core PRIVATE rdplib)
add_test(NAME rdplib_utime_core COMMAND rdplib_utime_core)
set_tests_properties(rdplib_utime_core PROPERTIES TIMEOUT 10)

add_executable(rdplib_uioctl_core uioctl_core.c)
target_link_libraries(rdplib_uioctl_core PRIVATE rdplib)
add_test(NAME rdplib_uioctl_core COMMAND rdplib_uioctl_core)
set_tests_properties(rdplib_uioctl_core PROPERTIES TIMEOUT 10)

add_executable(rdplib_uthread_core uthread_core.c)
target_link_libraries(rdplib_uthread_core PRIVATE rdplib)
add_test(NAME rdplib_uthread_core COMMAND rdplib_uthread_core)
set_tests_properties(rdplib_uthread_core PROPERTIES TIMEOUT 10)

add_executable(rdplib_usend_core usend_core.c)
target_link_libraries(rdplib_usend_core PRIVATE rdplib)
add_test(NAME rdplib_usend_core COMMAND rdplib_usend_core)

# Compile the recovered owner with only its two socket result seams renamed so
# all three historical return paths can be exercised deterministically.
add_executable(rdplib_usend_send_result_core usend_send_result_core.c ../rdplib/src/usend.c)
target_include_directories(rdplib_usend_send_result_core PRIVATE ../rdplib/include ../rdplib/src)
target_compile_definitions(rdplib_usend_send_result_core PRIVATE
    rdplib_platform_send_datagram=usend_test_send_datagram
    rdplib_platform_last_socket_error=usend_test_last_socket_error
)
target_link_libraries(rdplib_usend_send_result_core PRIVATE rdplib)
if(WIN32)
    target_include_directories(rdplib_usend_send_result_core PRIVATE ../rdplib/platform/windows)
else()
    target_include_directories(rdplib_usend_send_result_core PRIVATE ../rdplib/platform/posix)
endif()
add_test(NAME rdplib_usend_send_result_core COMMAND rdplib_usend_send_result_core)

add_executable(rdplib_ustrerror_core ustrerror_core.c)
target_link_libraries(rdplib_ustrerror_core PRIVATE rdplib)
add_test(NAME rdplib_ustrerror_core COMMAND rdplib_ustrerror_core)

add_executable(rdplib_serial_rx_core serial_rx_core.c)
target_link_libraries(rdplib_serial_rx_core PRIVATE rdplib)
add_test(NAME rdplib_serial_rx_core COMMAND rdplib_serial_rx_core)

add_executable(rdplib_serial_core serial_core.c ../rdplib/src/serial.c ../rdplib/src/list.c)
target_include_directories(rdplib_serial_core PRIVATE ../rdplib/include ../rdplib/src)
if(WIN32)
    target_include_directories(rdplib_serial_core PRIVATE ../rdplib/platform/windows)
else()
    target_include_directories(rdplib_serial_core PRIVATE ../rdplib/platform/posix)
    target_link_libraries(rdplib_serial_core PRIVATE Threads::Threads)
endif()
add_test(NAME rdplib_serial_core COMMAND rdplib_serial_core)
set_tests_properties(rdplib_serial_core PROPERTIES TIMEOUT 20)

# Compile tx.c with private backend names so the deterministic retry mock does
# not collide when dead only rdp_send makes the library pull the real usend.obj.
add_executable(rdplib_tx_retry tx_retry.c ../rdplib/src/tx.c)
target_compile_definitions(rdplib_tx_retry PRIVATE usend=tx_retry_test_usend rdplib_usend=tx_retry_test_rdplib_usend)
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
