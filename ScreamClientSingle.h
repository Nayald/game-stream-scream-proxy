//
// Created by xavier on 12/2/22.
//

#ifndef SCREAM_SCREAMCLIENTSINGLE_H
#define SCREAM_SCREAMCLIENTSINGLE_H

#include <unordered_map>
#include <thread>

#include "scream/code/ScreamRx.h"
#include "scream/code/RtpQueue.h"

#include "spinlock.h"
#include "SimpleBlock.h"
#include "Sink.h"
#include "Source.h"

class ScreamClientSingle : public SimpleBlock, public Sink, public Source {
public:
    static constexpr size_t UDP_BUFFER_SIZE = 65536;

private:
    int fd = -1;
    ScreamRx scream;

public:
    explicit ScreamClientSingle(std::string &&name);
    ~ScreamClientSingle() override = default;

    void init(const std::unordered_map<std::string, std::string> &params) override;

private:
    void run() override;
};


#endif //SCREAM_SCREAMCLIENTSINGLE_H
