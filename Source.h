//
// Created by xavier on 11/30/22.
//

#ifndef SCREAM_SOURCE_H
#define SCREAM_SOURCE_H

#include <unordered_map>
#include <unordered_set>

#include "concurrentqueue/blockingconcurrentqueue.h"

#include "spinlock.h"

struct Msg {
    enum MsgType {
        NONE,
        RAW,
        RTP_PACKET,
        RTCP_PACKET,
        BITRATE_REQUEST,
        IFRAME_REQUEST,
    };

    MsgType type = NONE;
    void *data = nullptr;
    ssize_t size = 0;
    uint64_t extra = 0;

    ~Msg() {
        if (data) {
            free(data);
        }
    }
};

using MsgQueue = moodycamel::BlockingConcurrentQueue<std::shared_ptr<const Msg>>;

class Source {
private:
    std::unordered_map<Msg::MsgType, std::unordered_set<std::shared_ptr<MsgQueue>>> other_queues;
    spinlock lock;

public:
    explicit Source() = default;
    virtual ~Source() = default;

    bool registerQueue(Msg::MsgType type, const std::shared_ptr<MsgQueue> &queue) {
        lock.lock();
        auto result = other_queues.try_emplace(type).first->second.emplace(queue);
        lock.unlock();
        return result.second;
    }

    bool unregisterQueue(Msg::MsgType type, const std::shared_ptr<MsgQueue> &queue) {
        lock.lock();
        bool result = false;
        auto it = other_queues.find(type);
        if (it != other_queues.end()) {
            result = it->second.erase(queue);
        }
        lock.unlock();
        return result;
    }

    void forward(const std::shared_ptr<const Msg> &msg) {
        lock.lock();
        auto it = other_queues.find(msg->type);
        if (it != other_queues.end()) {
            for (auto &queue : it->second) {
                queue->enqueue(msg);
            }
        }
        lock.unlock();
    }
};

#endif //SCREAM_SOURCE_H
