#include "tcp_server.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace net {

TcpServer::TcpServer(int port) : port_(port) {}
TcpServer::~TcpServer() { stop(); }

bool TcpServer::start() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port_);
    if (::bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) < 0) return false;
    if (::listen(listen_fd_, 16) < 0) return false;
    running_ = true;
    accept_thread_ = std::thread(&TcpServer::accept_loop, this);
    return true;
}

void TcpServer::stop() {
    if (!running_.exchange(false)) return;
    ::shutdown(listen_fd_, SHUT_RDWR);
    ::close(listen_fd_);
    listen_fd_ = -1;
    if (accept_thread_.joinable()) accept_thread_.join();
    std::lock_guard<std::mutex> lk(clients_mu_);
    for (auto& [fd, name] : clients_) ::close(fd);
    clients_.clear();
}

void TcpServer::accept_loop() {
    while (running_) {
        int fd = ::accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) { if (running_) continue; else break; }
        std::thread t(&TcpServer::client_loop, this, fd);
        t.detach();
    }
}

void TcpServer::client_loop(int fd) {
    std::string peer;
    {
        std::lock_guard<std::mutex> lk(clients_mu_);
        peer = "client-" + std::to_string(fd);
        clients_[fd] = peer;
    }
    if (on_connect_) on_connect_(peer);
    std::string buf;
    char chunk[4096];
    while (running_) {
        ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        buf.append(chunk, n);
        size_t nl;
        while ((nl = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::string resp = handler_ ? handler_(fd, peer, line) : "";
            if (!resp.empty()) send_to(fd, resp);
        }
    }
    {
        std::lock_guard<std::mutex> lk(clients_mu_);
        clients_.erase(fd);
    }
    if (on_disconnect_) on_disconnect_(peer);
    ::close(fd);
}

void TcpServer::send_to(int fd, const std::string& msg) {
    std::string data = msg + "\n";
    ::send(fd, data.data(), data.size(), MSG_NOSIGNAL);
}

void TcpServer::send_to_peer(const std::string& peer, const std::string& msg) {
    int fd = -1;
    {
        std::lock_guard<std::mutex> lk(clients_mu_);
        for (auto& [f, name] : clients_)
            if (name == peer) { fd = f; break; }
    }
    if (fd >= 0) send_to(fd, msg);
}

void TcpServer::broadcast(const std::string& msg) {
    std::lock_guard<std::mutex> lk(clients_mu_);
    for (auto& [fd, name] : clients_) send_to(fd, msg);
}

int TcpServer::client_count() const {
    std::lock_guard<std::mutex> lk(clients_mu_);
    return (int)clients_.size();
}

} // namespace net
