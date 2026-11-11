#pragma once
#include "event_bus.hpp"
#include "game_state.hpp"
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace game {

// Player slot in a lobby room. Before the game starts it carries the chosen
// nickname and class (scene "choose name & class"); position appears later.
struct Member {
    std::string client;              // peer name (connection id)
    std::string token;               // one-time id used to join this room
    std::string nickname;            // set via set_identity
    std::string char_class = "dwarf";// dwarf | goblin
    bool ready = false;
};

struct Room {
    std::string id;
    std::string name;
    std::shared_ptr<GameState> state;
    std::vector<Member> members;
    bool is_full() const { return members.size() >= static_cast<size_t>(state->config().max_players); }
    // {"id","name","players":n,"max":4,"members":[{nickname,class,ready}]}
    std::string status_json() const;
};

// Thread-safe registry of lobby rooms. Max 4 players per room (issue #3).
// Emits signals through EventBus: room_created, room_status, player_joined,
// player_left, game_started.
class RoomManager {
public:
    explicit RoomManager(EventBus* bus = nullptr) : bus_(bus) {}

    Room* create_room(const std::string& name, const std::string& client,
                      const std::string& token);
    bool join_room(const std::string& room_id, const std::string& client,
                   const std::string& token);
    bool leave_room(const std::string& room_id, const std::string& client);
    // Name & class selection (issue): assign nickname/class to a member.
    bool set_identity(const std::string& room_id, const std::string& client,
                      const std::string& nickname, const std::string& char_class);
    std::string room_of_token(const std::string& token);
    std::string room_of_client(const std::string& client);
    bool list_rooms(std::vector<std::string>& out_ids) const;
    Room* get_room(const std::string& room_id);
    void remove_room(const std::string& room_id);
    std::vector<std::string> all_status_json(); // snapshot for broadcasts

private:
    void emit(EventType t, const std::string& room_id, const std::string& payload);
    Room* find_locked(const std::string& room_id);

    mutable std::mutex mu_;
    std::map<std::string, Room> rooms_;
    int counter_ = 0;
    EventBus* bus_ = nullptr;
};

} // namespace game
