extern "C" {
#include <arpa/inet.h>
#include <byteswap.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
}

#include <cstring>

#include "logger.h"
#include "scream_utils.h"
#include "scream_v2_server_single.h"

constexpr uint32_t SSRC = 100;

ScreamV2ServerSingle::ScreamV2ServerSingle(std::string name, bool l4s)
    : SimpleBlock(std::move(name)), scream(0.7f, 0.7f, 0.06f, 12500, 1.5f, 1.5f, 2.0f, 0.05f, l4s, false, false, false) {}

ScreamV2ServerSingle::~ScreamV2ServerSingle() {
    if (fd >= 0) {
        close(fd);
    }
}

void ScreamV2ServerSingle::init(const std::unordered_map<std::string, std::string> &params) {
    if (!stop_condition.load(std::memory_order::relaxed)) {
        logger::log(logger::WARNING, name, ": you need to stop my thread first before re-init me");
        return;
    }

    sockaddr_in local_addr = {AF_INET, 0, {}, {}};
    sockaddr_in remote_addr = {AF_INET, 0, {}, {}};
    float max_bitrate = 30e6f;
    float min_bitrate = 1e6f;
    float start_bitrate = min_bitrate;
    for (auto const &[key, val] : params) {
        logger::log(logger::DEBUG, name, ": ", key, " = ", val);
        switch (hash(key)) {
            using namespace std::literals;
        case hash("local_addr"sv):
            inet_pton(AF_INET, val.c_str(), &local_addr.sin_addr.s_addr);
            break;
        case hash("local_port"sv):
            local_addr.sin_port = htons(std::stoi(val));
            break;
        case hash("remote_addr"sv):
            inet_pton(AF_INET, val.c_str(), &remote_addr.sin_addr.s_addr);
            break;
        case hash("remote_port"sv):
            remote_addr.sin_port = htons(std::stoi(val));
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

    const int ect = l4s ? 1 : 2; // ECN_ECT_0 = 2, ECN_ECT_1 = 1;
    if (setsockopt(fd, IPPROTO_IP, IP_TOS, &ect, sizeof(ect)) < 0) {
        logger::log(logger::ERROR, name, ": fail to set ecn ect bit -> ", std::strerror(errno));
    }

    if (bind(fd, (const sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        logger::log(logger::ERROR, name, ": fail to bind socket -> ", std::strerror(errno));
    }

    if (connect(fd, (const sockaddr *)(&remote_addr), sizeof(remote_addr)) < 0) {
        logger::log(logger::ERROR, name, ": fail to connect socket -> ", std::strerror(errno));
    }

    char local_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &local_addr.sin_addr.s_addr, local_ip, INET_ADDRSTRLEN);
    char remote_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &remote_addr.sin_addr.s_addr, remote_ip, INET_ADDRSTRLEN);
    logger::log(logger::INFO, name, ": will listen on ", local_ip, ':', ntohs(local_addr.sin_port), " and send data to ", remote_ip, ':',
                ntohs(remote_addr.sin_port));

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

    scream.registerNewStream(&rtp_queue, SSRC, 1.0f, min_bitrate, start_bitrate, max_bitrate, 0.2f, false, 0.0f);
    logger::log(logger::INFO, name, ": scream will contain bitrate in the range [", static_cast<uint32_t>(min_bitrate), ", ",
                static_cast<uint32_t>(max_bitrate), "] with a starting value of ", static_cast<uint32_t>(start_bitrate));
    initialized = true;
}

void ScreamV2ServerSingle::run() {
    std::thread lookup_thread(&ScreamV2ServerSingle::lookup, this);
    logger::log(logger::INFO, name, ": spawn an additional thread for lookup operations");
    std::thread read_thread(&ScreamV2ServerSingle::read, this);
    logger::log(logger::INFO, name, ": spawn an additional thread for read operations");

    logger::log(logger::DEBUG, name, ": listen thread pid is ", gettid());
    std::shared_ptr<const Msg> msg;
    while (!stop_condition.load(std::memory_order::relaxed)) {
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

        const uint16_t sequence_number = ntohs(*reinterpret_cast<const uint16_t *>(rtp_data + 2));
        const uint32_t timestamp = ntohl(*reinterpret_cast<const uint32_t *>(rtp_data + 4));
        const uint32_t ssrc = ntohl(*reinterpret_cast<const uint32_t *>(rtp_data + 8));
        const size_t header_size =
            12 + 4 * scrc_count + extension * 4 * ntohs(*reinterpret_cast<const uint16_t *>(rtp_data + 12 + 4 * scrc_count + 2));

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

void ScreamV2ServerSingle::lookup() {
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
            if (rtp_queue.pop(&data, size, ssrc, seq, is_marked)) {
                send(fd, data, size, 0);
                can_transmit = scream.addTransmitted(getTimeInNtp(), ssrc, size, seq, is_marked);
            } else {
                can_transmit = scream.isOkToTransmit(getTimeInNtp(), ssrc);
            }
        }

        lock.unlock();
        // std::this_thread::sleep_until(start +
        // std::chrono::duration<float>(can_transmit > 0 ? can_transmit : 500e-6));
        std::this_thread::sleep_until(start + std::chrono::microseconds(10));
    }
}

void ScreamV2ServerSingle::read() {
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
