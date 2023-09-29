#ifndef SCREAM_SCREAMSERVERSINGLE_H
#define SCREAM_SCREAMSERVERSINGLE_H

#include <thread>

#include "scream/code/RtpQueue.h"
#include "scream/code/ScreamTx.h"

#include "simple_block.h"
#include "sink.h"
#include "source.h"
#include "spinlock.h"

class ScreamServerSingle : public SimpleBlock, public Sink, public Source {
  public:
    static constexpr size_t UDP_BUFFER_SIZE = 1472;

    explicit ScreamServerSingle(std::string name, bool l4s = false, bool new_cc = false);
    ~ScreamServerSingle() override;

    void init(const std::unordered_map<std::string, std::string> &params) override;

  private:
    void run() override;
    void lookup();
    void read();

    int fd = -1;
    bool l4s;
    ScreamV1Tx scream;
    RtpQueue rtp_queue;
    uint32_t last_log = 0;
    spinlock lock;
};

#endif // SCREAM_SCREAMSERVERSINGLE_H
