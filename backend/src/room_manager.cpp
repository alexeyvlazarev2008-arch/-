#include "room_manager.hpp"
#include <sstream>

namespace game {

Room* RoomManager::create_room(const std::string& name) {
    std::lock_guard<std::mutex> lk(mu_);
    std::ostringstream id;
    id << "room-" << (++counter_) << "-" << name;
    Room r;
    r.id = id.str();
    Config cfg;
    r.state = std::make_shared<GameState>(cfg);
    auto [it, ok] = rooms_.emplace(r.id, std::move(r));
    return &it->second;
}

bool RoomManager::join_room(const std::string& room_id, const std::string& client) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) return false;
    Room& r = it->second;
    if (r.is_full()) return false;
    r.clients.push_back(client);
    r.state->add_player(client);
    return true;
}

bool RoomManager::list_rooms(std::vector<std::string>& out_ids) const {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [id, r] : rooms_) out_ids.push_back(id);
    return !rooms_.empty();
}

Room* RoomManager::get_room(const std::string& room_id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = rooms_.find(room_id);
    return it == rooms_.end() ? nullptr : &it->second;
}

void RoomManager::remove_room(const std::string& room_id) {
    std::lock_guard<std::mutex> lk(mu_);
    rooms_.erase(room_id);
}

} // namespace game
