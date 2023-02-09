//
// Created by xavier on 12/2/22.
//

#ifndef SCREAM_SCREAMSERVER_H
#define SCREAM_SCREAMSERVER_H

#include <unordered_map>
#include <thread>

#include "scream/code/ScreamTx.h"
#include "scream/code/RtpQueue.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "spinlock.h"
#include "SimpleBlock.h"
#include "Sink.h"
#include "Source.h"

struct RtpStreamInfo {
    std::chrono::steady_clock::time_point last_packet_arrival = std::chrono::steady_clock::now();
    RtpQueue queue;
};

class ScreamServer : public SimpleBlock, public Sink, public Source {
public:
    static constexpr size_t UDP_BUFFER_SIZE = 65536;

private:
    ScreamTx scream;
    std::unordered_map<uint32_t, RtpStreamInfo> rtp_queues;
    spinlock lock;
    int fd;
    alignas(64) uint8_t rx_buffer[UDP_BUFFER_SIZE];

    uint32_t last_log = 0;

public:
    explicit ScreamServer(std::string &&name);
    ~ScreamServer() override = default;

    void init(const std::unordered_map<std::string, std::string> &params) override;

private:
    void run() override;
    void lookup();
    void read();

    void handleRtp(const std::shared_ptr<const Msg> &msg);
    void handleRtcp(const std::shared_ptr<const Msg> &msg);
};


#endif //SCREAM_SCREAMSERVER_H
