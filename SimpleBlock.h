//
// Created by xavier on 12/28/22.
//

#ifndef SCREAM_SIMPLEBLOCK_H
#define SCREAM_SIMPLEBLOCK_H

#include <thread>
#include <unordered_map>

class SimpleBlock {
protected:
    std::string name;
    bool initialized = false;

    std::atomic<bool> stop_condition = true;
    std::thread thread;

public:
    explicit SimpleBlock(std::string &&name);
    virtual ~SimpleBlock() = default;

    static constexpr uint32_t hash(std::string_view str) noexcept{
        uint32_t hash = 5381;

        for(const char c : str) {
            hash = ((hash << 5) + hash) + static_cast<uint32_t>(c);
        }

        return hash;
    }

    virtual void init(const std::unordered_map<std::string, std::string> &params) = 0;

    void start();
    void stop();

protected:
    virtual void run() = 0;
};


#endif //SCREAM_SIMPLEBLOCK_H
