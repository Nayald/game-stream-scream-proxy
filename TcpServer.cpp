//
// Created by xavier on 1/5/23.
//
#include "TcpServer.h"

#include <cstring>

#include "logger.h"

TcpServer::TcpServer(std::string name) : SimpleBlock(std::move(name)) {

}

TcpServer::~TcpServer() noexcept {
    if (client_fd >= 0) {
        close(client_fd);
    }

    if (listen_fd >= 0) {
        close(listen_fd);
    }
}

void TcpServer::init(const std::unordered_map<std::string, std::string> &params) {
    sockaddr_in src_addr = {AF_INET, 0, {0}, {0}};
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
            default:
                logger::log(logger::WARNING, name, ": unknown key ", key);
                break;
        }
    }

    if (client_fd >= 0) {
        close(client_fd);
        client_fd = -1;
    }

    if (listen_fd >= 0) {
        close(listen_fd);
        listen_fd = -1;
    }

    listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    static constexpr int enable = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        logger::log(logger::ERROR, name, ": fail to set socket reuse port -> ", std::strerror(errno));
    }

    timeval tv = {.tv_sec = 0, .tv_usec = 100'000};
    if (setsockopt(listen_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
        logger::log(logger::ERROR, name, ": fail to set socket timeout -> ", std::strerror(errno));
    }

    if (bind(listen_fd, (const sockaddr*)&src_addr, sizeof(src_addr)) < 0) {
        logger::log(logger::ERROR, name, ": fail to bind socket -> ", std::strerror(errno));
    }

    if (listen(listen_fd, 1)) {
        logger::log(logger::ERROR, name, ": fail to listen socket -> ", std::strerror(errno));
    }

    char src_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &src_addr.sin_addr.s_addr, src_ip, INET_ADDRSTRLEN);
    logger::log(logger::INFO, name, ": will wait for connection on ", src_ip, ':', ntohs(src_addr.sin_port));

    initialized = true;
}

void TcpServer::run() {
    std::thread rx_thread(&TcpServer::read, this);
    std::thread ax_thread(&TcpServer::accept_client, this);

    std::shared_ptr<const Msg> msg;
    uint8_t msg_header[3] = {0xff, 0x00, 0x00};
    while (!stop_condition.load(std::memory_order::relaxed)) {
        if (!own_queue->wait_dequeue_timed(msg, WAIT_TIMEOUT_DELAY)) {
            continue;
        }

        if (msg->size <= 0 || msg->type != Msg::RAW || client_error.load(std::memory_order::relaxed)) {
            continue;
        }

        for (size_t i = 0; i < sizeof(msg_header) - 1; i++) {
            msg_header[sizeof(msg_header) - 1 - i] = (msg->size >> (8 * (sizeof(msg->size) - i))) & 0xff;
        }

        if (send(client_fd, msg_header, sizeof(msg_header), 0) < 0) {
            logger::log(logger::ERROR, name, ": error while sending header -> ", std::strerror(errno));
            client_error.store(true, std::memory_order::release);
            continue;
        }

        if (send(client_fd, msg->data, msg->size, 0) < 0) {
            logger::log(logger::ERROR, name, ": error while sending data -> ", std::strerror(errno));
            client_error.store(true, std::memory_order::release);
            continue;
        }
    }

    ax_thread.join();
    read_cv.notify_all();
    rx_thread.join();
}

void TcpServer::read() {
    logger::log(logger::INFO, name, ": spawn an additional thread for read operation with id ", gettid());
    uint8_t buffer[2 * TCP_BUFFER_SIZE];
    size_t offset = 0;
    ssize_t ret;
    while (!stop_condition.load(std::memory_order::relaxed)) {
        if (client_error.load(std::memory_order::relaxed)) {
            logger::log(logger::DEBUG, name, ": read thread enters into sleep mode");
            std::unique_lock lk(read_m);
            read_cv.wait(lk, [&error = client_error, &stop = stop_condition]{
                return !error.load(std::memory_order::relaxed) || stop.load(std::memory_order::relaxed);
            });
            logger::log(logger::DEBUG, name, ": read thread exits from sleep mode");
            continue;
        }

        ret = recv(client_fd.load(std::memory_order::relaxed), buffer + offset, TCP_BUFFER_SIZE, 0);
        if (ret < 0) {
            if (errno != EAGAIN || errno != EWOULDBLOCK) {
                logger::log(logger::ERROR, name, ": error while reading socket -> ", std::strerror(errno));
                client_error.store(true, std::memory_order::release);
            }
            continue;
        }

        // ret == 0 means disconnection
        if (ret == 0) {
            client_error.store(true, std::memory_order::release);
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

void TcpServer::accept_client() {
    logger::log(logger::INFO, name, ": spawn an additional thread for accept operation with id", gettid());
    sockaddr_in dst_addr;
    socklen_t len;
    char dst_ip[INET_ADDRSTRLEN];
    int ret;
    while (!stop_condition.load(std::memory_order::relaxed)) {
        while ((ret = accept(listen_fd, reinterpret_cast<sockaddr *>(&dst_addr), &len)) >= 0) {
            // if already one client connected just close incoming connection
            if (client_error.load(std::memory_order::relaxed)) {
                if (int fd = client_fd.exchange(ret, std::memory_order::acquire); fd >= 0) {
                    close(fd);
                }

                inet_ntop(AF_INET, &dst_addr.sin_addr, dst_ip, INET_ADDRSTRLEN);
                logger::log(logger::INFO, name, ": connection from ", dst_ip, ':', htons(dst_addr.sin_port), " accepted");
                timeval tv = {.tv_sec = 0, .tv_usec = 100'000};
                if (setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *) &tv, sizeof tv) < 0) {
                    logger::log(logger::ERROR, name, ": fail to set client socket timeout -> ", std::strerror(errno));
                }
                client_error.store(false, std::memory_order::release);
                read_cv.notify_all();
            } else {
                close(ret);
                logger::log(logger::INFO, name, ": drop connection, already connected");
            }
        }

        if (ret < 0 && (errno != EAGAIN || errno != EWOULDBLOCK)) {
            logger::log(logger::ERROR, name, ": error during accept ->", std::strerror(errno));
        }
    }
}