//
// Created by xavier on 11/30/22.
//

#ifndef SCREAM_SINK_H
#define SCREAM_SINK_H

#include <unordered_set>

#include "concurrentqueue/blockingconcurrentqueue.h"

#include "spinlock.h"
#include "Source.h"

class Sink {
public:
    static constexpr auto WAIT_TIMEOUT_DELAY = std::chrono::milliseconds(100);

protected:
    std::shared_ptr<MsgQueue> own_queue;

public:
    explicit Sink(size_t queue_size=64) : own_queue(std::make_shared<MsgQueue>(queue_size)) {}
    virtual ~Sink() = default;

    std::shared_ptr<MsgQueue> getQueue() {
        return own_queue;
    }
};

#endif //SCREAM_SINK_H
