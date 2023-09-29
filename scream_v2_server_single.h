#ifndef SCREAM_SCREAMSERVERSINGLEV2_H
#define SCREAM_SCREAMSERVERSINGLEV2_H

#include <thread>

#include "scream/code/RtpQueue.h"
#include "scream/code/ScreamTx.h"

#include "simple_block.h"
#include "sink.h"
#include "source.h"
#include "spinlock.h"

class ScreamV2ServerSingle : public SimpleBlock, public Sink, public Source {
  public:
    static constexpr size_t UDP_BUFFER_SIZE = 1472;

    explicit ScreamV2ServerSingle(std::string name, bool l4s = false);
    ~ScreamV2ServerSingle() override;

    void init(const std::unordered_map<std::string, std::string> &params) override;

  private:
    void run() override;
    void lookup();
    void read();

    int fd = -1;
    bool l4s = false;
    ScreamV2Tx scream;
    RtpQueue rtp_queue;
    uint32_t last_log = 0;
    spinlock lock;
};

#endif // SCREAM_SCREAMSERVERSINGLE_H
