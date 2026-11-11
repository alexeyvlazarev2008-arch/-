#pragma once
#include "game_state.hpp"
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace game {

struct Room {
    std::string id;
    std::shared_ptr<GameState> state;
    std::vector<std::string> clients;  // connected socket ids (names)
    bool is_full() const { return state->players().size() >= static_cast<size_t>(state->config().max_players); }
};

// Thread-safe registry of lobby rooms. Max 4 players per room (issue #3).
class RoomManager {
public:
    Room* create_room(const std::string& name);
    bool join_room(const std::string& room_id, const std::string& client);
    bool list_rooms(std::vector<std::string>& out_ids) const;
    Room* get_room(const std::string& room_id);
    void remove_room(const std::string& room_id);

private:
    mutable std::mutex mu_;
    std::map<std::string, Room> rooms_;
    int counter_ = 0;
};

} // namespace game
