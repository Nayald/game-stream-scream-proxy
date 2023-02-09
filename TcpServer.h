//
// Created by xavier on 1/5/23.
//

#ifndef SCREAM_TCPSERVER_H
#define SCREAM_TCPSERVER_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "condition_variable"

#include "SimpleBlock.h"
#include "Sink.h"
#include "Source.h"

class TcpServer : public SimpleBlock, public Sink, public Source{
public:
    static constexpr size_t TCP_BUFFER_SIZE = 65536;

private:
    int listen_fd = -1;
    std::atomic<int> client_fd = -1;
    std::atomic<bool> client_error = true;
    std::mutex read_m;
    std::condition_variable read_cv;

public:
    explicit TcpServer(std::string name);
    ~TcpServer() override;

    void init(const std::unordered_map<std::string, std::string> &params) override;

private:
    void run() override;
    void read();
    void accept_client();
};


#endif //SCREAM_TCPSERVER_H
