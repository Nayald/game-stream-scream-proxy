//
// Created by xavier on 12/2/22.
//

#include "ScreamClientSingle.h"

#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <byteswap.h>

#include "logger.h"
#include "scream_utils.h"

constexpr uint32_t SSRC = 100;

ScreamClientSingle::ScreamClientSingle(std::string &&name) : SimpleBlock(std::forward<std::string>(name)), scream(SSRC) {

}

void ScreamClientSingle::init(const std::unordered_map<std::string, std::string> &params) {
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

    initialized = true;
}

void ScreamClientSingle::run() {
    alignas(64) uint8_t buffer[UDP_BUFFER_SIZE];
    iovec rcv_iov = {buffer, sizeof(buffer)};
    uint8_t ctrl_buffer[8192] ;
    msghdr mhdr = {NULL, 0, &rcv_iov, 1, ctrl_buffer, sizeof(ctrl_buffer), 0};
    uint8_t tos;

    int ret;
    while (!stop_condition.load(std::memory_order::relaxed)) {
        ret = recvmsg(fd, &mhdr, 0);
        if (ret < 0) {
            if (errno != EAGAIN || errno != EWOULDBLOCK) {
                std::cerr << name << ": error while reading socket -> " << std::strerror(errno) << std::endl;
            }
            continue;
        }

        cmsghdr *cmhdr = CMSG_FIRSTHDR(&mhdr);
        while (cmhdr) {
            if (cmhdr->cmsg_level == IPPROTO_IP && cmhdr->cmsg_type == IP_TOS) {
                // read the TOS byte in the IP header
                tos = CMSG_DATA(cmhdr)[0];
                //logger::log(logger::DEBUG, name, ": tos = ", (int)tos);
            }
            cmhdr = CMSG_NXTHDR(&mhdr, cmhdr);
        }

        if (ret < 8) {
            continue;
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
        const size_t header_size = 12 + 4 * scrc_count + extension * 4 * bswap_16(*reinterpret_cast<const uint16_t *>(buffer + 12 + 4 * scrc_count + 2));

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

        scream.receive(getTimeInNtp(), buffer, SSRC, ret, sequence_number, 0);
        if ((scream.checkIfFlushAck() || marker) && scream.createStandardizedFeedback(getTimeInNtp(), marker, buffer, ret)) {
            send(fd, buffer, ret, 0);
        }
    }
}