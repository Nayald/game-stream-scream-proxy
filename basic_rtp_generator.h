#ifndef SCREAM_BASICRTPGENERATOR_H
#define SCREAM_BASICRTPGENERATOR_H

#include "simple_block.h"
#include "sink.h"
#include "source.h"

class BasicRtpGenerator : public SimpleBlock, public Sink, public Source {
  public:
    enum RtpType {
        NONE,
        AUDIO,
        VIDEO,
    };

    explicit BasicRtpGenerator(std::string name);
    ~BasicRtpGenerator() override = default;

    void init(const std::unordered_map<std::string, std::string> &params) override;

  private:
    void run() override;
    void listen();

    RtpType type = NONE;
    std::atomic<uint32_t> bitrate = 0;
    double framerate = 0;
    uint32_t ssrc = 0;
    uint16_t seq_number = 0;
    uint32_t timestamp = 0;
    uint32_t clock_freq = 0;
    uint32_t last_log = 0;
};

#endif // SCREAM_BASICRTPGENERATOR_H
