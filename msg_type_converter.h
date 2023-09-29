#ifndef SCREAM_MSGTYPECONVERTER_H
#define SCREAM_MSGTYPECONVERTER_H

#include <cstring>

#include "simple_block.h"
#include "sink.h"
#include "source.h"

template <Msg::MsgType U, Msg::MsgType V> class MsgTypeConverter : public SimpleBlock, public Sink, public Source {
  public:
    explicit MsgTypeConverter(std::string name) : SimpleBlock(std::move(name)) {}

    ~MsgTypeConverter() override = default;

    void init(const std::unordered_map<std::string, std::string> &params) override { initialized = true; }

  private:
    void run() override {
        std::shared_ptr<const Msg> msg;
        while (!stop_condition.load(std::memory_order::relaxed)) {
            if (own_queue->wait_dequeue_timed(msg, WAIT_TIMEOUT_DELAY) && msg->type == U) {
                forward(U == V ? msg : convert(msg));
            }
        }
    }

    std::shared_ptr<Msg> convert(const std::shared_ptr<const Msg> &msg) {
        static_assert((U == Msg::RAW && (V == Msg::RTP_PACKET || V == Msg::RTCP_PACKET)) ||
                          (V == Msg::RAW && (U == Msg::RTP_PACKET || U == Msg::RTCP_PACKET)),
                      "non-specialized template is for RAW to PACKET types and vice-versa");
        auto msg2 = std::make_shared<Msg>();
        msg2->type = V;
        msg2->data = std::aligned_alloc(64, msg->size);
        std::memcpy(msg2->data, msg->data, msg->size);
        msg2->size = msg->size;
        msg2->extra = msg->extra;
        return msg2;
    }
};

template <> std::shared_ptr<Msg> MsgTypeConverter<Msg::BITRATE_REQUEST, Msg::RAW>::convert(const std::shared_ptr<const Msg> &msg);

template <> std::shared_ptr<Msg> MsgTypeConverter<Msg::IFRAME_REQUEST, Msg::RAW>::convert(const std::shared_ptr<const Msg> &msg);

#endif // SCREAM_MSGTYPECONVERTER_H
