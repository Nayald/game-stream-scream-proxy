//
// Created by xavier on 12/2/22.
//

#include "ScreamServerSingle.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <byteswap.h>

#include "logger.h"
#include "scream_utils.h"

constexpr uint32_t SSRC = 100;

ScreamServerSingle::ScreamServerSingle(std::string &&name, bool ecn, bool new_cc) : SimpleBlock(std::forward<std::string>(name)), ecn(ecn), scream(0.9f, 0.9f, 0.06f, false, 1.0f, 10.0f, 12500, 1.25f, 20, ecn, false, false, 2.0f, new_cc) {

}

ScreamServerSingle::~ScreamServerSingle() noexcept {
    if (fd >= 0) {
        close(fd);
    }
}

void ScreamServerSingle::init(const std::unordered_map<std::string, std::string> &params) {
    if (!stop_condition.load(std::memory_order::relaxed)) {
        logger::log(logger::WARNING, name, ": you need to stop my thread first before re-init me");
        return;
    }

    sockaddr_in src_addr = {AF_INET, 0, {0}, {0}};
    sockaddr_in dst_addr = {AF_INET, 0, {0}, {0}};
    float max_bitrate = 30e6f;
    float min_bitrate = 1e6f;
    float start_bitrate = min_bitrate;
    for (auto const& [key, val] : params) {
        logger::log(logger::DEBUG, name, ": ", key, " = ", val);
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
            case hash("max_bitrate"sv):
                max_bitrate = std::stof(val);
                break;
            case hash("min_bitrate"sv):
                min_bitrate = std::stof(val);
                break;
            case hash("start_bitrate"sv):
                start_bitrate = std::stof(val);
                break;
            default:
                logger::log(logger::WARNING, name, ": unknown key ", key);
                break;
        }
    }

    if (fd >= 0) {
        close(fd);
    }

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    const int enable = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        logger::log(logger::ERROR, name, ": fail to set socket reuse port -> ", std::strerror(errno));
    }

    const timeval tv = {.tv_sec = 0, .tv_usec = 100'000};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        logger::log(logger::ERROR, name, ": fail to set socket timeout -> ", std::strerror(errno));
    }

    if (ecn) {
        const int ect = 1; // ECN_ECT_0 = 2, ECN_ECT_1 = 1;
        if (setsockopt(fd, IPPROTO_IP, IP_TOS, &ect, sizeof(ect)) < 0) {
            logger::log(logger::ERROR, name, ": fail to set ecn ect bit -> ", std::strerror(errno));
        }
    }

    if (bind(fd, (const sockaddr*)&src_addr, sizeof(src_addr)) < 0) {
        logger::log(logger::ERROR, name, ": fail to bind socket -> ", std::strerror(errno));
    }

    if (connect(fd, (const sockaddr*)(&dst_addr), sizeof(dst_addr)) < 0) {
        logger::log(logger::ERROR, name, ": fail to connect socket -> ", std::strerror(errno));
    }

    char src_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &src_addr.sin_addr.s_addr, src_ip, INET_ADDRSTRLEN);
    char dst_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &dst_addr.sin_addr.s_addr, dst_ip, INET_ADDRSTRLEN);
    logger::log(logger::INFO, name, ": will listen on ", src_ip, ':', ntohs(src_addr.sin_port), " and send data to ", dst_ip, ':', ntohs(dst_addr.sin_port));

    if (max_bitrate > 100e6f) {
        max_bitrate = 100e6f;
    }

    if (min_bitrate < 64e3f) {
        min_bitrate = 64e3f;
    }

    if (max_bitrate < min_bitrate) {
        max_bitrate = min_bitrate;
    }

    if (min_bitrate > max_bitrate) {
        min_bitrate = max_bitrate;
    }

    if (start_bitrate < min_bitrate) {
        start_bitrate = min_bitrate;
    }

    if (start_bitrate > max_bitrate) {
        start_bitrate = max_bitrate;
    }

    scream.registerNewStream(&rtp_queue, SSRC, 1.0f, min_bitrate, start_bitrate, max_bitrate,
                             10e6, 0.5f, 0.2f, 0.1f, 0.05f, 0.9f, 0.9f, false, 0.0f);
    logger::log(logger::INFO, name, ": scream will contain bitrate in the range [", static_cast<uint32_t>(min_bitrate), ", ", static_cast<uint32_t>(max_bitrate), "] with a starting value of ", static_cast<uint32_t>(start_bitrate));
    initialized = true;
}

void ScreamServerSingle::run() {
    std::thread lookup_thread(&ScreamServerSingle::lookup, this);
    logger::log(logger::INFO, name, ": spawn an additional thread for lookup operations");
    std::thread read_thread(&ScreamServerSingle::read, this);
    logger::log(logger::INFO, name, ": spawn an additional thread for read operations");

    logger::log(logger::DEBUG, name, ": listen thread pid is ", gettid());
    std::shared_ptr<const Msg> msg;
    while(!stop_condition.load(std::memory_order::relaxed)) {
        if (!own_queue->wait_dequeue_timed(msg, WAIT_TIMEOUT_DELAY)) {
            continue;
        }

        if (msg->type != Msg::RTP_PACKET || msg->size < 12) {
            logger::log(logger::DEBUG, name, ": got unknown message type or message size too small");
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

        const uint16_t sequence_number = ntohs(*reinterpret_cast<const uint16_t*>(rtp_data + 2));
        const uint32_t timestamp = ntohl(*reinterpret_cast<const uint32_t*>(rtp_data + 4));
        const uint32_t ssrc = ntohl(*reinterpret_cast<const uint32_t*>(rtp_data + 8));
        const size_t header_size = 12 + 4 * scrc_count + extension * 4 * ntohs(*reinterpret_cast<const uint16_t *>(rtp_data + 12 + 4 * scrc_count + 2));

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

        void *packet = std::aligned_alloc(64, msg->size);
        std::memcpy(packet, rtp_data, msg->size);
        const uint32_t time = getTimeInNtp();

        lock.lock();
        rtp_queue.push(packet, static_cast<int>(msg->size), SSRC, sequence_number, marker, static_cast<const float>(time) / 65536.0f);
        scream.newMediaFrame(time, SSRC, static_cast<int>(msg->size), marker);
        lock.unlock();
    }

    lookup_thread.join();
    read_thread.join();
}

void ScreamServerSingle::lookup() {
    logger::log(logger::DEBUG, name, ": lookup thread pid is ", gettid());

    uint32_t ssrc;
    int size;
    uint16_t seq;
    bool is_marked;
    void *data;
    while (!stop_condition.load(std::memory_order::relaxed)) {
        const auto start = std::chrono::steady_clock::now();
        lock.lock();
        float can_transmit = scream.isOkToTransmit(getTimeInNtp(), ssrc);
        while (can_transmit == 0 && rtp_queue.sizeOfQueue() > 0) {
            if (rtp_queue.pop(&data, size, ssrc, seq, is_marked)) { ;
                send(fd, data, size, 0);
                can_transmit = scream.addTransmitted(getTimeInNtp(), ssrc, size, seq, is_marked);
            } else {
                can_transmit = scream.isOkToTransmit(getTimeInNtp(), ssrc);
            }
        }

        lock.unlock();
        //std::this_thread::sleep_until(start + std::chrono::duration<float>(can_transmit > 0 ? can_transmit : 500e-6));
        std::this_thread::sleep_until(start + std::chrono::microseconds(10));
    }
}

void ScreamServerSingle::read() {
    logger::log(logger::DEBUG, name, ": read thread pid is ", gettid());

    alignas(64) uint8_t buffer[UDP_BUFFER_SIZE];
    while (!stop_condition.load(std::memory_order::relaxed)) {
        ssize_t size = recv(fd, buffer, UDP_BUFFER_SIZE, 0);
        if (size < 0) {
            if (errno != EAGAIN || errno != EWOULDBLOCK) {
                std::cerr << name << ": error while reading socket -> " << std::strerror(errno) << std::endl;
            }
            continue;
        }

        if (size < 8) {
            continue;
        }

        const uint8_t version = buffer[0] >> 6;
        const bool padding = (buffer[0] >> 5) & 0b001;
        const uint8_t report_count = buffer[0] & 0b00011111;
        const uint8_t packet_type = buffer[1];
        const uint16_t length = bswap_16(*reinterpret_cast<const uint16_t *>(buffer + 2));
        const uint32_t ssrc = bswap_32(*reinterpret_cast<const uint32_t *>(buffer + 4));
        if (ssrc != SSRC) {
            *reinterpret_cast<uint32_t *>(buffer + 4) = SSRC;
        }

        /*std::cout << "new rtp packet: "
        << "version=" << (int)version
        << ", padding=" << padding
        << ", report_count=" << int(report_count)
        << ", type=" << int(packet_type)
        << ", length=" << length
        << ", ssrc=" << ssrc
        << std::endl;*/

        const uint32_t time = getTimeInNtp();
        auto msg = std::make_shared<Msg>();

        lock.lock();
        scream.incomingStandardizedFeedback(time, buffer, static_cast<int>(size));
        msg->size = static_cast<ssize_t>(scream.getTargetBitrate(SSRC));
        lock.unlock();

        msg->type = msg->size > 0 ? Msg::BITRATE_REQUEST : Msg::IFRAME_REQUEST;
        forward(msg);

        if (time - last_log > 2 * 65536) {
            char log[160];
            scream.getStatistics(static_cast<const float>(time) / 65536.0f, log);
            logger::log(logger::INFO, name, ": ", log);
            last_log = time;
        }
    }
}