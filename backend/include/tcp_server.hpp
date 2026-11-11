#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace net {

// Simple POSIX TCP server: newline-delimited text protocol, one thread per client.
// Callback signature: (client_fd, peer_name, line) -> response (sent back as a line).
using MsgHandler = std::function<std::string(int, const std::string&, const std::string&)>;

class TcpServer {
public:
    explicit TcpServer(int port);
    ~TcpServer();
    void set_handler(MsgHandler h) { handler_ = std::move(h); }
    bool start();            // non-blocking accept loop in background thread
    void stop();
    void broadcast(const std::string& msg);          // to all clients
    void send_to(int fd, const std::string& msg);
    int client_count() const;

private:
    void accept_loop();
    void client_loop(int fd);

    int port_;
    int listen_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    MsgHandler handler_;
    mutable std::mutex clients_mu_;
    std::map<int, std::string> clients_;  // fd -> peer name
};

} // namespace net
