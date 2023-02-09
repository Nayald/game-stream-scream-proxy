//
// Created by xavier on 1/5/23.
//

#ifndef SCREAM_TCPCLIENT_H
#define SCREAM_TCPCLIENT_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "SimpleBlock.h"
#include "Sink.h"
#include "Source.h"

class TcpClient : public SimpleBlock, public Sink, public Source{
public:
    static constexpr size_t TCP_BUFFER_SIZE = 65536;

private:
    int fd = -1;

public:
    explicit TcpClient(std::string name);
    ~TcpClient() override;

    void init(const std::unordered_map<std::string, std::string> &params) override;

private:
    void run() override;
    void read();
};


#endif //SCREAM_TCPCLIENT_H
