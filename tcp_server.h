#ifndef SCREAM_TCPSERVER_H
#define SCREAM_TCPSERVER_H

#include "condition_variable"

#include "simple_block.h"
#include "sink.h"
#include "source.h"

class TcpServer : public SimpleBlock, public Sink, public Source {
  public:
    static constexpr size_t TCP_BUFFER_SIZE = 4096;

    explicit TcpServer(std::string name);
    ~TcpServer() override;

    void init(const std::unordered_map<std::string, std::string> &params) override;

  private:
    void run() override;
    void read();
    void accept_client();

    int listen_fd = -1;
    std::atomic<int> client_fd = -1;
    std::atomic<bool> client_error = true;
    std::mutex read_m;
    std::condition_variable read_cv;
};

#endif // SCREAM_TCPSERVER_H
