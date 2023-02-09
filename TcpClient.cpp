//
// Created by xavier on 1/5/23.
//
#include <iostream>

#include "TcpClient.h"

#include <cstring>

#include "logger.h"

TcpClient::TcpClient(std::string name) : SimpleBlock(std::move(name)) {

}

TcpClient::~TcpClient() noexcept {
    if (fd > 0) {
        close(fd);
    }
}

void TcpClient::init(const std::unordered_map<std::string, std::string> &params) {
    sockaddr_in src_addr = {AF_INET, 0, {0}, {0}};
    sockaddr_in dst_addr = {AF_INET, 0, {0}, {0}};
    for (auto const& [key, val] : params) {
        logger::log(logger::DEBUG, name ,": ", key, " = ", val);
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

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    static constexpr int enable = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        logger::log(logger::ERROR, name, ": fail to set socket reuse port -> ", std::strerror(errno));
    }

    timeval tv = {.tv_sec = 0, .tv_usec = 100'000};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
        std::cerr << name << ": fail to set socket timeout -> " << std::strerror(errno) << std::endl;
    }

    if (bind(fd, (const sockaddr *) &src_addr, sizeof(src_addr)) < 0) {
        std::cerr << name << ": fail to bind socket -> " << std::strerror(errno) << std::endl;
    }

    if (connect(fd, (const sockaddr*)(&dst_addr), sizeof(dst_addr)) < 0) {
        std::cerr << name << ": fail to connect socket -> " << std::strerror(errno) << std::endl;
    }

    char src_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &src_addr.sin_addr.s_addr, src_ip, INET_ADDRSTRLEN);
    char dst_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &dst_addr.sin_addr.s_addr, dst_ip, INET_ADDRSTRLEN);
    logger::log(logger::INFO, name, ": will receive data on ", src_ip, ':', ntohs(src_addr.sin_port), " and send data to ", dst_ip, ':', ntohs(dst_addr.sin_port));

    initialized = true;
}

void TcpClient::run() {
    std::thread rx_thread(&TcpClient::read, this);

    std::shared_ptr<const Msg> msg;
    uint8_t msg_header[3] = {0xff, 0x00, 0x00};
    while (!stop_condition.load(std::memory_order::relaxed)) {
        if (!own_queue->wait_dequeue_timed(msg, WAIT_TIMEOUT_DELAY)) {
            continue;
        }

        if (msg->size <= 0 || msg->type != Msg::RAW) {
            continue;
        }

        for (size_t i = 0; i < sizeof(msg_header) - 1; i++) {
            msg_header[sizeof(msg_header) - 1 - i] = (msg->size >> (8 * (sizeof(msg->size) - i))) & 0xff;
        }

        if (send(fd, msg_header, sizeof(msg_header), 0) < 0) {
            logger::log(logger::ERROR, name, ": error while sending header -> ", std::strerror(errno));
            continue;
        }

        if (send(fd, msg->data, msg->size, 0) < 0) {
            logger::log(logger::ERROR, name, ": error while sending data -> ", std::strerror(errno));
            continue;
        }
    }

    rx_thread.join();
}

void TcpClient::read() {
    logger::log(logger::INFO, name, ": spawn an additional thread for read operation with id ", gettid());
    uint8_t buffer[2 * TCP_BUFFER_SIZE];
    size_t offset = 0;
    ssize_t ret;
    while (!stop_condition.load(std::memory_order::relaxed)) {
        ret = recv(fd, buffer + offset, TCP_BUFFER_SIZE, 0);
        if (ret < 0) {
            if (errno != EAGAIN || errno != EWOULDBLOCK) {
                logger::log(logger::ERROR, name, ": error while reading socket -> ", std::strerror(errno));
            }
            continue;
        }

        // ret == 0 means disconnection
        if (ret == 0) {
            continue;
        }

        //std::memcpy(buffer + offset, rx_buffer, ret);
        offset += ret;

        size_t start_offset = 0;
        static constexpr uint8_t delimiter = 0xff;
        uint16_t msg_size;
        while (start_offset + sizeof(delimiter) + sizeof(msg_size) <= offset) {
            if (buffer[start_offset] != delimiter) {
                start_offset += sizeof(delimiter);
                continue;
            }

            msg_size = ntohs(*reinterpret_cast<uint16_t*>(buffer + start_offset + sizeof(delimiter)));
            // incomplete message
            if (start_offset + msg_size + sizeof(msg_size) + sizeof(delimiter) > offset) {
                break;
            }

            auto msg = std::make_shared<Msg>();
            msg->type = Msg::RAW;
            msg->data = std::aligned_alloc(64, msg_size);
            std::memcpy(msg->data, buffer + start_offset + sizeof(msg_size) + sizeof(delimiter), msg_size);
            msg->size = msg_size;
            forward(msg);

            std::cout.write(static_cast<const char *>(msg->data), msg->size) << std::endl;

            start_offset += msg_size + sizeof(msg_size) + sizeof(delimiter);
        };

        // consume read data
        std::memmove(buffer, buffer + start_offset, offset -= start_offset);
    }
}