//
// Created by xavier on 12/28/22.
//

#include <iostream>
#include <random>
#include <cstring>
#include <netinet/in.h>

#include "BasicRtpGenerator.h"

#include "logger.h"
#include "scream_utils.h"

constexpr size_t BUFFER_SIZE = 1408; // 64 * 22

BasicRtpGenerator::BasicRtpGenerator(std::string &&name) : SimpleBlock(std::forward<std::string>(name)) {

}

void BasicRtpGenerator::init(const std::unordered_map<std::string, std::string> &params) {
    if (!stop_condition.load(std::memory_order::relaxed)) {
        logger::log(logger::WARNING, name, ": you need to stop my thread first before re-init me");
        return;
    }

    bitrate = 0;
    framerate = 0;
    clock_freq = 0;
    ssrc = 0;
    seq_number = 0;
    timestamp = 0;
    for (const auto& [key, val] : params) {
        logger::log(logger::DEBUG, name, ": ", key, " = ", val);
        switch (hash(key)) {
            using namespace std::literals;
            case hash("type"sv): {
                static const std::unordered_map<std::string, RtpType> RTPTYPE_MAPPING = {
                        {"audio", AUDIO},
                        {"Audio", AUDIO},
                        {"AUDIO", AUDIO},
                        {"a", AUDIO},
                        {"A", AUDIO},
                        {"video", VIDEO},
                        {"Video", VIDEO},
                        {"VIDEO", VIDEO},
                        {"v", VIDEO},
                        {"V", VIDEO},
                };

                const auto it = RTPTYPE_MAPPING.find(val);
                type = it != RTPTYPE_MAPPING.end() ? it->second : NONE;
                break;
            }
            case hash("bitrate"sv): {
                bitrate = std::stoi(val);
                break;
            }
            case hash("framerate"sv): {
                framerate = std::stod(val);
                break;
            }
            case hash("clock_freq"sv): {
                clock_freq = std::stoi(val);
                break;
            }
            case hash("ssrc"sv): {
                ssrc = std::stoi(val);
                break;
            }
            default: {
                logger::log(logger::WARNING, name, ": unknown key ", key);
            }
        }
    }

    if (type == NONE) {
        logger::log(logger::INFO, name, ": type not defined, set to video");
        type = VIDEO;
    }

    if (bitrate == 0) {
        logger::log(logger::INFO, name, ": bitrate not defined, set to 1.000.000");
        bitrate = 1'000'000;
    }

    if (framerate == 0) {
        logger::log(logger::INFO, name, ": framerate not defined, set to 50");
        framerate = 50;
    }

    if (clock_freq == 0) {
        logger::log(logger::INFO, name, ": clock frequency not defined, set to ", (clock_freq = type == VIDEO ? 90000 : 48000));
    }

    if (ssrc == 0) {
        static std::random_device rd;
        static std::mt19937 mt(rd());
        static std::uniform_int_distribution<uint32_t> dist(0, UINT32_MAX);
        ssrc = dist(mt);
    }

    logger::log(logger::INFO, name, ": initialized with values {bitrate=", bitrate, ", framerate=", framerate, ", ssrc=", ssrc, ", clock_freq=", clock_freq, '}');
    initialized = true;
}

void BasicRtpGenerator::run() {
    std::thread listen_queue(&BasicRtpGenerator::listen, this);
    logger::log(logger::INFO, name, ": spawn an additional thread for events");

    logger::log(logger::DEBUG, name, ": packet generator thread pid is ", gettid());
    alignas(64) uint8_t buffer[BUFFER_SIZE];
    while (!stop_condition.load(std::memory_order::relaxed)) {
        const auto start = std::chrono::steady_clock::now();
        auto frame_size = static_cast<uint32_t>(bitrate.load(std::memory_order::relaxed) / (8.0f * framerate));
        buffer[0] = 0b10000000;
        buffer[1] = 0b01100000 + (type == AUDIO);
        //*reinterpret_cast<uint32_t*>(buffer + 4) = htonl(timestamp);
        *reinterpret_cast<uint32_t*>(buffer + 4) = htonl(static_cast<uint32_t>(getTimeInNtp() / 65536.0 * 90000));
        //timestamp += static_cast<uint32_t>(round(clock_freq / framerate));
        *reinterpret_cast<uint32_t*>(buffer + 8) = htonl(ssrc);
        while (frame_size > 0) {
            auto msg = std::make_shared<Msg>();
            msg->extra = getTimeInNtp();
            msg->type = Msg::RTP_PACKET;
            msg->size = frame_size <= 1396 ? frame_size: 1396; // payload size
            frame_size -= msg->size;
            buffer[1] |= uint8_t{frame_size <= 0} << 7;
            *reinterpret_cast<uint16_t*>(buffer + 2) = htons(seq_number++);
            msg->size += 12; // header size
            msg->data = std::aligned_alloc(64, msg->size);
            std::memcpy(msg->data, buffer, msg->size);
            forward(msg);
        }

        std::this_thread::sleep_until(start + std::chrono::duration<double>(1.0 / framerate));
    }

    listen_queue.join();
}

void BasicRtpGenerator::listen() {
    logger::log(logger::DEBUG, name, ": listen thread pid is ", gettid());
    std::shared_ptr<const Msg> msg;
    while (!stop_condition.load(std::memory_order::relaxed)) {
        if (!own_queue->wait_dequeue_timed(msg, WAIT_TIMEOUT_DELAY)) {
            continue;
        }

        switch (msg->type) {
            case Msg::BITRATE_REQUEST:
                bitrate.store(msg->size, std::memory_order::relaxed);
                //std::cout << name << ": set bitrate to " << msg->size << std::endl;
                break;
            default:
                logger::log(logger::DEBUG, name, ": got unknown message type");
                break;
        }

        if (uint32_t time = getTimeInNtp(); time - last_log > 2 * 65536) {
            logger::log(logger::INFO, name, ": current bitrate is ", bitrate);
            last_log = time;
        }
    }
}