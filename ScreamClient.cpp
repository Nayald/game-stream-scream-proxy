//
// Created by xavier on 12/2/22.
//

#include "ScreamClient.h"

#include <iostream>
#include <cstring>
#include <byteswap.h>

#include "scream_utils.h"

ScreamClient::ScreamClient(std::string &&name) : SimpleBlock(std::forward<std::string>(name)) {

}

void ScreamClient::init(const std::unordered_map<std::string, std::string> &params) {
    if (!stop_condition.load(std::memory_order::relaxed)) {
        std::cout << name << ": you need to stop my thread first before re-init me" << std::endl;
        return;
    }

    initialized = true;
}

void ScreamClient::run() {
    std::thread lookup_thread(&ScreamClient::lookup, this);
    std::cout << name << ": spawn an additional thread for read operation" << std::endl;

    std::shared_ptr<const Msg> msg;
    alignas(64) uint8_t buffer[1472];
    int size;
    while(!stop_condition.load(std::memory_order::relaxed)) {
        if (!own_queue->wait_dequeue_timed(msg, WAIT_TIMEOUT_DELAY) || msg->size < 12) {
            continue;
        }

        auto *const rtp_data = static_cast<const uint8_t *const>(msg->data);
        /* |-0--2-|-3-|-4-|-5--8-|-9-|-10--16-|-17--31-| (bits)
           | Vers | P | X |  CC  | M |  Type  | seq nb | */
        const uint8_t version = rtp_data[0] >> 6;
        const bool padding = (rtp_data[0] >> 5) & 0b001;
        const bool extension = (rtp_data[0] >> 4) & 0b0001;
        const uint8_t scrc_count = rtp_data[0] & 0b00001111;

        const bool marker = rtp_data[1] >> 7;
        const uint8_t payload_type = rtp_data[1] & 0b01111111;

        const uint16_t sequence_number = bswap_16(*reinterpret_cast<const uint16_t *>(rtp_data + 2));
        const uint32_t timestamp = bswap_32(*reinterpret_cast<const uint32_t *>(rtp_data + 4));
        uint32_t ssrc = bswap_32(*reinterpret_cast<const uint32_t *>(rtp_data + 8));
        const size_t header_size = 12 + 4 * scrc_count + extension * 4 * bswap_16(
                *reinterpret_cast<const uint16_t *>(rtp_data + 12 + 4 * scrc_count + 2));

        /*std::cout << "new rtp packet: "
        << "version=" << (int)version
        << ", padding=" << padding
        << ", extension=" << extension
        << ", scrc count=" << (int)scrc_count
        << ", marker=" << marker
        << ", payload type=" << (int)payload_type
        << ", sequence number=" << sequence_number
        << ", timestamp=" << timestamp
        << ", ssrc=" << ssrc
        << ", total header size=" << header_size
        << std::endl;
        if (marker) {
            std::cout << "end of frame!" << std::endl;
        }*/

        lock.lock();
        auto it = screams.try_emplace(ssrc, ssrc).first;
        if (abs(sequence_number - it->second.last_sequence_number) > 1) {
            std::cout << "received non consecutive packet " << sequence_number << " instead of " << it->second.last_sequence_number + 1 << std::endl;
        }
        it->second.last_sequence_number = sequence_number;

        it->second.scream.receive(getTimeInNtp(), msg->data, ssrc, msg->size, sequence_number, 0);
        it->second.last_packet_arrival_time = std::chrono::steady_clock::now();
        if ((it->second.scream.checkIfFlushAck() || marker) && it->second.scream.createStandardizedFeedback(getTimeInNtp(), marker, buffer, size)) {
            auto rtcp_msg = std::make_shared<Msg>();
            rtcp_msg->type = Msg::RTCP_PACKET;
            rtcp_msg->data = aligned_alloc(64, size);
            std::memcpy(rtcp_msg->data, buffer, size);
            rtcp_msg->size = size;
            forward(rtcp_msg);
        }
        lock.unlock();
    }

    lookup_thread.join();
    std::cout << name << ": all thread stopped" << std::endl;
}

void ScreamClient::lookup() {
    std::chrono::steady_clock::time_point time;
    std::vector<uint32_t> staled_keys;
    while (!stop_condition.load(std::memory_order::relaxed)) {
        time = std::chrono::steady_clock::now();
        lock.lock();
        for (auto& it : screams) {
            uint32_t ssrc = it.first;
            RtpStreamInfo &info = it.second;
            if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - info.last_packet_arrival_time).count() > 1) {
                staled_keys.push_back(ssrc);
                continue;
            }
        }

        for (const auto key : staled_keys) {
            screams.erase(key);
        }

        lock.unlock();
        staled_keys.clear();
        std::this_thread::sleep_until(time + std::chrono::milliseconds (500));
    }
}