//
// Created by xavier on 12/2/22.
//

#include "ScreamServer.h"

#include <cstring>
#include <byteswap.h>
static double t0 = 0;

static void packet_free(void *buf, uint32_t ssrc) {
    free(buf);
}

static uint32_t getTimeInNtp() {
    timeval tp;
    gettimeofday(&tp, NULL);
    double time = tp.tv_sec + tp.tv_usec * 1e-6 - t0;
    uint64_t ntp64 = time * 65536.0;
    uint32_t ntp = 0xFFFFFFFF & ntp64;
    return ntp;
}

ScreamServer::ScreamServer(std::string &&name) : SimpleBlock(std::forward<std::string>(name)), scream(0.9f, 0.9f, 0.06f, false, 1.0f, 10.0f, 12500, 1.25f, 20, false, false, false, 2.0f, false) {

}

void ScreamServer::init(const std::unordered_map<std::string, std::string> &params) {
    if (!stop_condition.load(std::memory_order::relaxed)) {
        std::cout << name << ": you need to stop my thread first before re-init me" << std::endl;
        return;
    }

    sockaddr_in src_addr = {AF_INET, 0, {0}, {0}};
    sockaddr_in dst_addr = {AF_INET, 0, {0}, {0}};
    for (auto const& [key, val] : params) {
        std::cout << name << ": " << key << " = " << val << std::endl;
        switch (hash(key)) {
            using namespace std::literals;
            case hash("src_addr"sv):
                inet_pton(AF_INET, val.c_str(), &src_addr.sin_addr.s_addr);
                break;
            case hash("src_port"sv):
                src_addr.sin_port = htons(std::stoi(val));
                break;
            case hash("dst_addr"sv):
                inet_pton(AF_INET, val.c_str(), &dst_addr.sin_addr.s_addr);
                break;
            case hash("dst_port"sv):
                dst_addr.sin_port = htons(std::stoi(val));
                break;
            default:
                std::cout << name << ": unknown key " << key << std::endl;
                break;
        }
    }

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    timeval tv = {.tv_sec = 0, .tv_usec = 100'000};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
        std::cerr << name << ": fail to set socket timeout -> " << std::strerror(errno) << std::endl;
    }

    if (bind(fd, (const sockaddr*)&src_addr, sizeof(src_addr)) < 0) {
        std::cerr << name << ": fail to bind socket -> " << std::strerror(errno) << std::endl;
    }

    if (connect(fd, (const sockaddr*)(&dst_addr), sizeof(dst_addr)) < 0) {
        std::cerr << name << ": fail to connect socket -> " << std::strerror(errno) << std::endl;
    }

    char src_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &src_addr.sin_addr.s_addr, src_ip, INET_ADDRSTRLEN);
    char dst_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &dst_addr.sin_addr.s_addr, dst_ip, INET_ADDRSTRLEN);
    std::cout << name << ": will listen on " << src_ip << ':' << ntohs(src_addr.sin_port) << " and send data to " << dst_ip << ':' << ntohs(dst_addr.sin_port) << std::endl;


    struct timeval tp;
    gettimeofday(&tp, NULL);
    t0 = tp.tv_sec + tp.tv_usec * 1e-6 - 1e-3;
    initialized = true;
}

void ScreamServer::run() {
    std::thread lookup_thread(&ScreamServer::lookup, this);
    std::cout << name << ": spawn an additional thread for lookup operations" << std::endl;
    std::thread read_thread(&ScreamServer::read, this);
    std::cout << name << ": spawn an additional thread for read operations" << std::endl;

    std::shared_ptr<const Msg> msg;
    while(!stop_condition.load(std::memory_order::relaxed)) {
        if (!own_queue->wait_dequeue_timed(msg, WAIT_DELAY)) {
            continue;
        }

        switch (msg->type) {
            case Msg::RTP:
                handleRtp(msg);
                break;
            case Msg::RTCP:
                handleRtcp(msg);
                break;
            default:
                std::cout << name << ": got unknown message type" << std::endl;
                break;
        }
    }

    lookup_thread.join();
}

void ScreamServer::lookup() {
    int size;
    uint16_t seq;
    bool is_marked;
    std::chrono::steady_clock::time_point time;
    std::vector<uint32_t> staled_keys;
    void *tx_buffer;
    while (!stop_condition.load(std::memory_order::relaxed)) {
        time = std::chrono::steady_clock::now();
        lock.lock();
        for (auto& it : rtp_queues) {
            uint32_t ssrc = it.first;
            RtpStreamInfo &info = it.second;
            if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - info.last_packet_arrival).count() > 1 && info.queue.sizeOfQueue() == 0) {
                staled_keys.push_back(ssrc);
                continue;
            }

            float can_transmit = scream.isOkToTransmit(getTimeInNtp(), ssrc);
            while (can_transmit == 0) {
                //auto msg = std::make_shared<Msg>();
                //msg->type = Msg::RTP;
                //if (info.queue.pop(&msg->data, size, ssrc, seq, is_marked)) {
                if (info.queue.pop(&tx_buffer, size, ssrc, seq, is_marked)) {
                    //msg->size = size;
                    //forward(msg);
                    send(fd, tx_buffer, size, 0);
                    can_transmit = scream.addTransmitted(getTimeInNtp(), ssrc, size, seq, is_marked);
                } else {
                    can_transmit = scream.isOkToTransmit(getTimeInNtp(), ssrc);
                }
            }
        }

        for (const auto key : staled_keys) {
            rtp_queues.erase(key);
        }

        lock.unlock();
        staled_keys.clear();
        std::this_thread::sleep_until(time + std::chrono::microseconds(10));
    }
}

void ScreamServer::handleRtp(const std::shared_ptr<const Msg> &msg) {
    if (msg->size < 12) {
        return;
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

    const uint16_t sequence_number = bswap_16(*reinterpret_cast<const uint16_t*>(rtp_data + 2));
    const uint32_t timestamp = bswap_32(*reinterpret_cast<const uint32_t*>(rtp_data + 4));
    const uint32_t ssrc = bswap_32(*reinterpret_cast<const uint32_t*>(rtp_data + 8));
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
    auto it = rtp_queues.find(ssrc);
    if (it == rtp_queues.end()) {
        auto result = rtp_queues.try_emplace(ssrc);
        if (result.second) {
            std::cout << name << ": new queue for ssrc " << ssrc << std::endl;
            it = result.first;
            scream.registerNewStream(&it->second.queue, ssrc, 1.0f,
                                     1'000'000, 1'000'000, 10'000'000,
                                     10'000'000, 0.5f, 0.2f, 0.1f, 0.05f, 0.9f, 0.9f, false, 0.0f);
        }
    }

    void *packet = std::aligned_alloc(64, msg->size);
    std::memcpy(packet, msg->data, msg->size);
    uint32_t time = getTimeInNtp();
    it->second.queue.push(packet, static_cast<int>(msg->size), ssrc, sequence_number, marker, static_cast<float>(time) / 65536.0f);
    scream.newMediaFrame(time, ssrc, static_cast<int>(msg->size), marker);
    it->second.last_packet_arrival = std::chrono::steady_clock::now();
    lock.unlock();
}

void ScreamServer::handleRtcp(const std::shared_ptr<const Msg> &msg) {
    if (msg->size < 8) {
        return;
    }

    auto *const rtcp_data = static_cast<const uint8_t *const>(msg->data);
    /* |-0--2-|-3-|-5--8-|-8--16-|-17--31-| (bits)
       | Vers | P |  RC  |  PT   | length | */
    const uint8_t version = rtcp_data[0] >> 6;
    const bool padding = (rtcp_data[0] >> 5) & 0b001;
    const uint8_t report_count = rtcp_data[0] & 0b00011111;
    const uint8_t packet_type = rtcp_data[1];
    const uint16_t length = bswap_16(*reinterpret_cast<const uint16_t*>(rtcp_data + 2));
    const uint32_t ssrc = bswap_32(*reinterpret_cast<const uint32_t*>(rtcp_data + 4));

    /*std::cout << "new rtp packet: "
    << "version=" << (int)version
    << ", padding=" << padding
    << ", report_count=" << int(report_count)
    << ", type=" << int(packet_type)
    << ", length=" << length
    << ", ssrc=" << ssrc
    << std::endl;*/

    lock.lock();
    if (rtp_queues.contains(ssrc)) {
        uint32_t time = getTimeInNtp();
        scream.incomingStandardizedFeedback(time, reinterpret_cast<uint8_t *>(msg->data), static_cast<int>(msg->size));
        auto msg2 = std::make_shared<Msg>();
        msg2->type = Msg::BITRATE_REQ;
        msg2->size = static_cast<ssize_t>(scream.getTargetBitrate(ssrc));
        if (msg2->size > 0) {
            forward(msg);
        }

        if (time - last_log > 2 * 65536) {
            char log[256];
            scream.getStatistics(static_cast<float>(time) / 65536.0f, log);
            std::cout << log << std::endl;
            last_log = time;
        }
    }
    lock.unlock();
}

void ScreamServer::read() {
    while (!stop_condition.load(std::memory_order::relaxed)) {
        ssize_t size = recv(fd, rx_buffer, UDP_BUFFER_SIZE, 0);
        if (size > 0) {
            const uint8_t version = rx_buffer[0] >> 6;
            const bool padding = (rx_buffer[0] >> 5) & 0b001;
            const uint8_t report_count = rx_buffer[0] & 0b00011111;
            const uint8_t packet_type = rx_buffer[1];
            const uint16_t length = bswap_16(*reinterpret_cast<const uint16_t*>(rx_buffer + 2));
            const uint32_t ssrc = bswap_32(*reinterpret_cast<const uint32_t*>(rx_buffer + 4));

            /*std::cout << "new rtp packet: "
            << "version=" << (int)version
            << ", padding=" << padding
            << ", report_count=" << int(report_count)
            << ", type=" << int(packet_type)
            << ", length=" << length
            << ", ssrc=" << ssrc
            << std::endl;*/

            lock.lock();
            if (rtp_queues.contains(ssrc)) {
                uint32_t time = getTimeInNtp();
                scream.incomingStandardizedFeedback(time, rx_buffer, static_cast<int>(size));
                auto msg2 = std::make_shared<Msg>();
                msg2->type = Msg::BITRATE_REQ;
                msg2->size = static_cast<ssize_t>(scream.getTargetBitrate(ssrc));
                if (msg2->size > 0) {
                    forward(msg);
                }

                if (time - last_log > 2 * 65536) {
                    char log[256];
                    scream.getStatistics(static_cast<float>(time) / 65536.0f, log);
                    std::cout << log << std::endl;
                    last_log = time;
                }
            }
            lock.unlock();
        } else if (size < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            std::cerr << name << ": error while reading socket -> " << std::strerror(errno) << std::endl;
        }
    }
}