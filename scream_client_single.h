#ifndef SCREAM_SCREAMCLIENTSINGLE_H
#define SCREAM_SCREAMCLIENTSINGLE_H

#include <unordered_map>

#include "scream/code/RtpQueue.h"
#include "scream/code/ScreamRx.h"

#include "simple_block.h"
#include "sink.h"
#include "source.h"
#include "spinlock.h"

class ScreamClientSingle : public SimpleBlock, public Sink, public Source {
  public:
    static constexpr size_t UDP_BUFFER_SIZE = 1472;

    explicit ScreamClientSingle(std::string name);
    ~ScreamClientSingle() override = default;

    void init(const std::unordered_map<std::string, std::string> &params) override;

  private:
    void run() override;
    void periodicRtcp();

    int fd = -1;
    ScreamRx scream;
    spinlock lock;
};

#endif // SCREAM_SCREAMCLIENTSINGLE_H
