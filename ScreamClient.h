//
// Created by xavier on 12/2/22.
//

#ifndef SCREAM_SCREAMCLIENT_H
#define SCREAM_SCREAMCLIENT_H

#include <unordered_map>
#include <thread>

#include "scream/code/ScreamRx.h"
#include "scream/code/RtpQueue.h"

#include "spinlock.h"
#include "SimpleBlock.h"
#include "Sink.h"
#include "Source.h"

struct RtpStreamInfo {
    std::chrono::steady_clock::time_point last_packet_arrival_time;
    ScreamRx scream;
    uint16_t last_sequence_number = 0;

    explicit RtpStreamInfo(uint32_t ssrc) : scream(ssrc) {

    }
};

class ScreamClient : public SimpleBlock, public Sink, public Source {
private:
    std::unordered_map<uint32_t, RtpStreamInfo> screams;
    spinlock lock;

public:
    explicit ScreamClient(std::string &&name);
    ~ScreamClient() override = default;

    void init(const std::unordered_map<std::string, std::string> &params) override;

private:
    void run() override;
    void lookup();
};


#endif //SCREAM_SCREAMCLIENT_H
