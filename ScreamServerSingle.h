//
// Created by xavier on 12/2/22.
//

#ifndef SCREAM_SCREAMSERVER_H
#define SCREAM_SCREAMSERVER_H

#include <thread>

#include "scream/code/ScreamTx.h"
#include "scream/code/RtpQueue.h"

#include "spinlock.h"
#include "SimpleBlock.h"
#include "Sink.h"
#include "Source.h"

class ScreamServerSingle : public SimpleBlock, public Sink, public Source {
public:
    static constexpr size_t UDP_BUFFER_SIZE = 65536;

private:
    int fd = -1;
    const bool ecn;
    ScreamTx scream;
    RtpQueue rtp_queue;
    uint32_t last_log = 0;
    spinlock lock;

public:
    explicit ScreamServerSingle(std::string &&name, bool ecn=false, bool new_cc=false);
    ~ScreamServerSingle() override;

    void init(const std::unordered_map<std::string, std::string> &params) override;

private:
    void run() override;
    void lookup();
    void read();
};


#endif //SCREAM_SCREAMSERVER_H
