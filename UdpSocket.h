//
// Created by xavier on 1/5/23.
//

#ifndef SCREAM_UDPSOCKET_H
#define SCREAM_UDPSOCKET_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "SimpleBlock.h"
#include "Sink.h"
#include "Source.h"

class UdpSocket : public SimpleBlock, public Sink, public Source{
public:
    static constexpr size_t UDP_BUFFER_SIZE = 65536;

private:
    int fd = -1;
    sockaddr_in dst_addr = {AF_INET, 0, {0}, {0}};

public:
    explicit UdpSocket(std::string name);
    ~UdpSocket() override;

    void init(const std::unordered_map<std::string, std::string> &params) override;

private:
    void run() override;
    void read();
};


#endif //SCREAM_UDPSOCKET_H
