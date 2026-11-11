#pragma once
// One-time session tokens ("One Time ID"). Each client receives a unique OTID
// when it connects; the server issues a fresh one after use (login), so a
// stolen/replayed token cannot be reused twice.
#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>

namespace game {

class IdentityStore {
public:
    // Generate a new one-time id bound to a peer (socket fd name).
    std::string issue(const std::string& peer) {
        std::lock_guard<std::mutex> lk(mu_);
        std::string tok = random_token();
        tokens_[tok] = peer;
        by_peer_[peer] = tok;
        return tok;
    }

    // Validate & consume a token for peer. Returns true on success; the token
    // is removed and a fresh replacement is generated (returned in `next`).
    bool redeem(const std::string& token, const std::string& peer, std::string& next) {
        if (token.empty()) return false;
        std::lock_guard<std::mutex> lk(mu_);
        auto it = tokens_.find(token);
        if (it == tokens_.end() || it->second != peer) return false;
        tokens_.erase(it);
        std::string fresh = random_token();
        tokens_[fresh] = peer;
        by_peer_[peer] = fresh;
        next = fresh;
        return true;
    }

    // Current outstanding token for a peer (empty if none).
    std::string current(const std::string& peer) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = by_peer_.find(peer);
        return it == by_peer_.end() ? std::string() : it->second;
    }

    void revoke_peer(const std::string& peer) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = by_peer_.find(peer);
        if (it == by_peer_.end()) return;
        tokens_.erase(it->second);
        by_peer_.erase(it);
    }

    size_t outstanding() {
        std::lock_guard<std::mutex> lk(mu_);
        return tokens_.size();
    }

private:
    static std::string random_token() {
        static thread_local std::mt19937_64 rng{std::random_device{}()};
        std::uniform_int_distribution<uint64_t> d;
        uint64_t v = d(rng);
        char buf[17];
        for (int i = 0; i < 16; ++i) buf[i] = hex[(v >> (60 - 4 * i)) & 0xF];
        buf[16] = 0;
        return std::string(buf);
    }
    static constexpr const char* hex = "0123456789abcdef";

    std::mutex mu_;
    std::unordered_map<std::string, std::string> tokens_;  // token -> peer
    std::unordered_map<std::string, std::string> by_peer_; // peer -> token
};

} // namespace game
