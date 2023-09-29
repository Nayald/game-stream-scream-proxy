cmake_minimum_required(VERSION 3.19)
project(scream)

set(CMAKE_CXX_STANDARD 20)
add_compile_options(-Wall -Wextra -O2 -march=native)

find_package(Threads REQUIRED)

add_executable(scream_server
        main_server.cpp
        simple_block.cpp simple_block.h
        source.h sink.h

        scream/code/ScreamTx.cpp scream/code/ScreamTx.h	
        scream/code/ScreamV2Tx.cpp scream/code/ScreamV2Tx.h	
        scream/code/ScreamV2TxStream.cpp
        scream/code/RtpQueue.cpp scream/code/RtpQueue.h
        scream_utils.h scream_utils.cpp
        scream_v2_server_single.cpp scream_v2_server_single.h

        basic_rtp_generator.cpp basic_rtp_generator.h

        udp_socket.cpp udp_socket.h
        tcp_server.cpp tcp_server.h
        tcp_client.cpp tcp_client.h
        msg_type_converter.cpp msg_type_converter.h

        logger.cpp logger.h
)

target_link_libraries(scream_server PRIVATE Threads::Threads)

add_executable(scream_client
        main_client.cpp
        simple_block.cpp simple_block.h
        source.h sink.h

        scream/code/ScreamRx.cpp scream/code/ScreamRx.h
        scream_client_single.cpp scream_client_single.h
        scream_utils.h scream_utils.cpp

        udp_socket.cpp udp_socket.h
        tcp_server.cpp tcp_server.h
        tcp_client.cpp tcp_client.h
        msg_type_converter.cpp msg_type_converter.h

        logger.cpp logger.h
)

target_link_libraries(scream_client PRIVATE Threads::Threads)

