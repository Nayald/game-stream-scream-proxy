//
// Created by xavier on 1/25/23.
//

#include "MsgTypeConverter.h"

template<>
std::shared_ptr<Msg> MsgTypeConverter<Msg::BITRATE_REQUEST, Msg::RAW>::convert(const std::shared_ptr<const Msg> &msg) {
    auto msg2 = std::make_shared<Msg>();
    msg2->type = Msg::RAW;
    msg2->data = aligned_alloc(64, 64);
    static constexpr std::string_view json = R"({"t":"n","v":)";
    std::memcpy(msg2->data, json.data(), json.size());
    auto val = std::to_string(msg->size);
    std::memcpy(static_cast<char *>(msg2->data) + json.size(), val.c_str(), val.size());
    static_cast<char *>(msg2->data)[json.size() + val.size()] = '}';
    msg2->size = json.size() + val.size() + 1;
    return msg2;
}

template<>
std::shared_ptr<Msg> MsgTypeConverter<Msg::IFRAME_REQUEST, Msg::RAW>::convert(const std::shared_ptr<const Msg> &msg) {
    static constexpr std::string_view json = R"({"t":"n","v":-1})";
    auto msg2 = std::make_shared<Msg>();
    msg2->type = Msg::RAW;
    msg2->data = aligned_alloc(64, json.size());
    std::memcpy(msg2->data, json.data(), json.size());
    msg2->size = json.size();
    return msg2;
}