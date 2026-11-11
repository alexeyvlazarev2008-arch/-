#pragma once
// Signal/slot event bus for the backend (issue: "система сигналов").
// Thread-safe. Rooms emit typed events; the server subscribes and forwards
// them to the right clients as JSON messages.
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace game {

enum class EventType : uint8_t {
    RoomCreated = 1,
    RoomStatusChanged = 2, // player joined/left, name/class set, room closed
    PlayerJoined = 3,
    PlayerLeft = 4,
    IdentityRequested = 5, // client connected without a valid one-time token
    GameStarted = 6,
    StateChanged = 7,
};

struct Event {
    EventType type;
    std::string room_id;
    std::string payload_json; // ready-to-send JSON body (without outer envelope)
};

using SubscriberId = uint64_t;

class EventBus {
public:
    using Handler = std::function<void(const Event&)>;

    SubscriberId subscribe(Handler h) {
        std::lock_guard<std::mutex> lk(mu_);
        SubscriberId id = ++counter_;
        subs_.emplace(id, std::move(h));
        return id;
    }

    void unsubscribe(SubscriberId id) {
        std::lock_guard<std::mutex> lk(mu_);
        subs_.erase(id);
    }

    void emit(Event e) {
        // Copy handlers under lock, invoke outside to avoid deadlocks.
        std::vector<Handler> snapshot;
        {
            std::lock_guard<std::mutex> lk(mu_);
            snapshot.reserve(subs_.size());
            for (auto& [id, h] : subs_) snapshot.push_back(h);
        }
        for (auto& h : snapshot) h(e);
    }

private:
    std::mutex mu_;
    std::map<SubscriberId, Handler> subs_;
    SubscriberId counter_ = 0;
};

} // namespace game
