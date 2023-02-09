cmake_minimum_required(VERSION 3.19)
project(scream)

set(CMAKE_CXX_STANDARD 20)
add_compile_options(-Wall -O0 -march=native -mavx2 -ftree-vectorize)

add_executable(scream_server
        main_server.cpp
        SimpleBlock.cpp SimpleBlock.h
        Source.h Sink.h

        scream/code/ScreamTx.cpp scream/code/ScreamTx.h
        scream/code/RtpQueue.cpp scream/code/RtpQueue.h
        scream_utils.h scream_utils.cpp
        ScreamServerSingle.cpp ScreamServerSingle.h

        BasicRtpGenerator.cpp BasicRtpGenerator.h

        UdpSocket.cpp UdpSocket.h
        TcpServer.cpp TcpServer.h
        TcpClient.cpp TcpClient.h
        MsgTypeConverter.cpp MsgTypeConverter.h

        logger.cpp logger.h
)

add_executable(scream_client
        main_client.cpp
        SimpleBlock.cpp SimpleBlock.h
        Source.h Sink.h

        scream/code/ScreamRx.cpp scream/code/ScreamRx.h
        ScreamClientSingle.cpp ScreamClientSingle.h
        scream_utils.h scream_utils.cpp

        UdpSocket.cpp UdpSocket.h
        TcpServer.cpp TcpServer.h
        TcpClient.cpp TcpClient.h
        MsgTypeConverter.cpp MsgTypeConverter.h

        logger.cpp logger.h
)
