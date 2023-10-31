extern "C" {
#include <arpa/inet.h>
#include <byteswap.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
}

#include <cstring>

#include <random>

#include "logger.h"
#include "scream_client_single.h"
#include "scream_utils.h"

constexpr uint32_t SSRC = 100;

ScreamClientSingle::ScreamClientSingle(std::string name) : SimpleBlock(std::move(name)), scream(SSRC) {}

void ScreamClientSingle::init(const std::unordered_map<std::string, std::string> &params) {
    if (!stop_condition.load(std::memory_order::relaxed)) {
        logger::log(logger::WARNING, name, ": you need to stop my thread first before re-init me");
        return;
    }

    sockaddr_in local_addr = {AF_INET, 0, {}, {}};
    sockaddr_in remote_addr = {AF_INET, 0, {}, {}};
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

    constexpr uint8_t set = 0x03;
    if (setsockopt(fd, IPPROTO_IP, IP_RECVTOS, &set, sizeof(set)) < 0) {
        logger::log(logger::ERROR, name, ": fail to set socket recvtos -> ", std::strerror(errno));
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

    initialized = true;
}

void ScreamClientSingle::run() {
    std::thread lookup_thread(&ScreamClientSingle::periodicRtcp, this);
    logger::log(logger::INFO, name, ": spawn an additional thread for periodic RTCP");

    alignas(64) uint8_t buffer[UDP_BUFFER_SIZE];
    iovec rcv_iov = {buffer, sizeof(buffer)};
    uint8_t ctrl_buffer[8192];
    msghdr mhdr = {NULL, 0, &rcv_iov, 1, ctrl_buffer, sizeof(ctrl_buffer), 0};
    uint8_t tos = 0;

    /*std::random_device dev;
    std::mt19937 rng(dev());
    std::uniform_real_distribution<float> rand(0, 1);*/

    int ret;
    while (!stop_condition.load(std::memory_order::relaxed)) {
        ret = recvmsg(fd, &mhdr, 0);
        if (ret < 0) {
            if (errno != EAGAIN || errno != EWOULDBLOCK) {
                std::cerr << name << ": error while reading socket -> " << std::strerror(errno) << std::endl;
            }
            continue;
        }

        if (ret < 8) {
            continue;
        }

        for (cmsghdr *cmhdr = CMSG_FIRSTHDR(&mhdr); cmhdr != nullptr; cmhdr = CMSG_NXTHDR(&mhdr, cmhdr)) {
            if (cmhdr->cmsg_level == IPPROTO_IP && cmhdr->cmsg_type == IP_TOS) {
                // read the TOS byte in the IP header
                tos = CMSG_DATA(cmhdr)[0];
                // logger::log(logger::DEBUG, name, ": tos = ", (int)tos);
            }
        }

        /* |-0--2-|-3-|-4-|-5--8-|-9-|-10--16-|-17--31-| (bits)
           | Vers | P | X |  CC  | M |  Type  | seq nb | */
        const uint8_t version = buffer[0] >> 6;
        const bool padding = (buffer[0] >> 5) & 0b001;
        const bool extension = (buffer[0] >> 4) & 0b0001;
        const uint8_t scrc_count = buffer[0] & 0b00001111;

        const bool marker = buffer[1] >> 7;
        const uint8_t payload_type = buffer[1] & 0b01111111;

        const uint16_t sequence_number = bswap_16(*reinterpret_cast<const uint16_t *>(buffer + 2));
        const uint32_t timestamp = bswap_32(*reinterpret_cast<const uint32_t *>(buffer + 4));
        uint32_t ssrc = bswap_32(*reinterpret_cast<const uint32_t *>(buffer + 8));
        const size_t header_size =
            12 + 4 * scrc_count + extension * 4 * bswap_16(*reinterpret_cast<const uint16_t *>(buffer + 12 + 4 * scrc_count + 2));

        /*std::cout << "new rtp packet: "
        << "version=" << (int)version
        << ", padding=" << padding
        << ", extension=" << extension
        << ", scrc count=" << (int)scrc_count
        << ", marker=" << marker
        << ", payload type=" << (int)payload_type
        << ", sequence number=" << sequence_number
        << ", timestamp=" << timestamp
        << ", ssrc =" << ssrc
        << ", total header ret=" << header_size
        << std::endl;
        if (marker) {
            std::cout << "end of frame!" << std::endl;
        }*/

        auto msg = std::make_shared<Msg>();
        msg->type = Msg::RTP_PACKET;
        msg->data = aligned_alloc(64, ret);
        std::memcpy(msg->data, buffer, ret);
        msg->size = ret;
        forward(msg);

        /*if (rand(rng) < 0.02) {
            tos |= 0x03;
        }*/

        lock.lock();
        scream.receive(getTimeInNtp(), 0, SSRC, ret, sequence_number, tos & 0x03, marker);
        if ((scream.checkIfFlushAck() || marker) && scream.createStandardizedFeedback(getTimeInNtp(), marker, buffer, ret)) {
            send(fd, buffer, ret, 0);
        }
        lock.unlock();
    }
}

void ScreamClientSingle::periodicRtcp() {
    alignas(64) unsigned char buffer[1536];
    int size;
    while (!stop_condition.load(std::memory_order::relaxed)) {
        const uint32_t ntp_time = getTimeInNtp();
        if (scream.isFeedback(ntp_time) && (scream.checkIfFlushAck() || (ntp_time - scream.getLastFeedbackT() > scream.getRtcpFbInterval()))) {
            lock.lock();
            if (scream.createStandardizedFeedback(ntp_time, true, buffer, size)) {
                send(fd, buffer, size, 0);
            }
            lock.unlock();
        }
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
}
