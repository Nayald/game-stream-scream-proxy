#ifndef SCREAM_UDPSOCKET_H
#define SCREAM_UDPSOCKET_H

extern "C" {
#include <netinet/in.h>
}

#include "simple_block.h"
#include "sink.h"
#include "source.h"

class UdpSocket : public SimpleBlock, public Sink, public Source {
  public:
    static constexpr size_t UDP_BUFFER_SIZE = 1472;

    explicit UdpSocket(std::string name);
    ~UdpSocket() override;

    void init(const std::unordered_map<std::string, std::string> &params) override;

  private:
    void run() override;
    void read();

    int fd = -1;
    sockaddr_in remote_addr;
};

#endif // SCREAM_UDPSOCKET_H
