#ifndef SCREAM_TCPCLIENT_H
#define SCREAM_TCPCLIENT_H

#include "simple_block.h"
#include "sink.h"
#include "source.h"

class TcpClient : public SimpleBlock, public Sink, public Source {
  public:
    static constexpr size_t TCP_BUFFER_SIZE = 4096;

    explicit TcpClient(std::string name);
    ~TcpClient() override;

    void init(const std::unordered_map<std::string, std::string> &params) override;

  private:
    void run() override;
    void read();

    int fd = -1;
};

#endif // SCREAM_TCPCLIENT_H
