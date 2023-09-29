extern "C" {
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
}

#include <cstring>

#include "logger.h"
#include "udp_socket.h"

UdpSocket::UdpSocket(std::string name) : SimpleBlock(std::move(name)) {}

UdpSocket::~UdpSocket() {
    if (fd >= 0) {
        close(fd);
    }
}

void UdpSocket::init(const std::unordered_map<std::string, std::string> &params) {
    sockaddr_in local_addr = {AF_INET, 0, {}, {}};
    remote_addr = {AF_INET, 0, {}, {}};
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
        fd = -1;
    }

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    static constexpr int enable = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        logger::log(logger::ERROR, name, ": fail to set socket reuse port -> ", std::strerror(errno));
    }

    static constexpr int recv_buf_size = 2 << 20;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &recv_buf_size, sizeof(recv_buf_size)) < 0) {
        logger::log(logger::ERROR, name, ": fail to set socket buffer size -> ", std::strerror(errno));
    }

    timeval tv = {.tv_sec = 0, .tv_usec = 100'000};
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv) < 0) {
        logger::log(logger::ERROR, name, ": fail to set socket timeout -> ", std::strerror(errno));
    }

    if (bind(fd, (const sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        logger::log(logger::ERROR, name, ": fail to bind socket -> ", std::strerror(errno));
    }

    /*if (connect(fd, (const sockaddr *)(&remote_addr), sizeof(remote_addr)) < 0) {
        logger::log(logger::ERROR, name, ": fail to connect socket -> ", std::strerror(errno));
    }*/

    char local_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &local_addr.sin_addr.s_addr, local_ip, INET_ADDRSTRLEN);
    char remote_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &remote_addr.sin_addr.s_addr, remote_ip, INET_ADDRSTRLEN);
    logger::log(logger::INFO, name, ": will listen on ", local_ip, ':', ntohs(local_addr.sin_port), " and send data to ", remote_ip, ':',
                ntohs(remote_addr.sin_port));

    initialized = true;
}

void UdpSocket::run() {
    std::thread rx_thread(&UdpSocket::read, this);
    logger::log(logger::INFO, name, ": spawn an additional thread for read operation");

    std::shared_ptr<const Msg> msg;
    while (!stop_condition.load(std::memory_order::relaxed)) {
        if (own_queue->wait_dequeue_timed(msg, WAIT_TIMEOUT_DELAY) && msg->size > 0) {
            ssize_t size = sendto(fd, msg->data, msg->size, 0, reinterpret_cast<sockaddr *>(&remote_addr), sizeof(remote_addr));
            if (size < 0) {
                logger::log(logger::ERROR, name, ": error while sending data -> ", std::strerror(errno));
            }
        }
    }

    rx_thread.join();
}

void UdpSocket::read() {
    alignas(64) uint8_t rx_buffer[UDP_BUFFER_SIZE];
    sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    while (!stop_condition.load(std::memory_order::relaxed)) {
        ssize_t ret = recvfrom(fd, rx_buffer, UDP_BUFFER_SIZE, 0, reinterpret_cast<sockaddr *>(&addr), &addr_len);
        if (ret < 0) {
            if (errno != EAGAIN || errno != EWOULDBLOCK) {
                std::cerr << name << ": error while reading socket -> " << std::strerror(errno) << std::endl;
            }
            continue;
        }

        if (ret <= 0) {
            continue;
        }

        // std::cout << ret << std::endl;
        auto msg = std::make_shared<Msg>();
        msg->type = Msg::RAW;
        msg->data = std::aligned_alloc(64, ret);
        std::memcpy(msg->data, rx_buffer, ret);
        msg->size = ret;
        forward(msg);
    }
}
