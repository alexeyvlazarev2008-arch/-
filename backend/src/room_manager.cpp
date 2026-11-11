#include "room_manager.hpp"
#include "json_codec.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace game {

// Sanitize a room name into an ASCII-safe id fragment (fixes the earlier bug
// where Cyrillic/spaces leaked into room ids).
static std::string slugify(const std::string& name) {
    std::string out;
    for (unsigned char c : name) {
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
        else if (c == '_' || c == '-') out += '_';
        // non-ASCII (e.g. Cyrillic) is dropped from the id, kept in Room::name
    }
    if (out.empty()) out = "lobby";
    if (out.size() > 24) out.resize(24);
    return out;
}

std::string Room::status_json() const {
    std::ostringstream o;
    o << "{\"id\":\"" << net::json_escape(id) << "\",\"name\":\"" << net::json_escape(name)
      << "\",\"players\":" << members.size() << ",\"max\":" << state->config().max_players
      << ",\"members\":[";
    for (size_t i = 0; i < members.size(); ++i) {
        const auto& m = members[i];
        if (i) o << ",";
        o << "{\"nickname\":\"" << net::json_escape(m.nickname)
          << "\",\"class\":\"" << net::json_escape(m.char_class)
          << "\",\"ready\":" << (m.ready ? "true" : "false") << "\"}";
    }
    o << "]}";
    return o.str();
}

void RoomManager::emit(EventType t, const std::string& room_id, const std::string& payload) {
    if (!bus_) return;
    bus_->emit(Event{t, room_id, payload});
}

Room* RoomManager::find_locked(const std::string& room_id) {
    auto it = rooms_.find(room_id);
    return it == rooms_.end() ? nullptr : &it->second;
}

Room* RoomManager::create_room(const std::string& name, const std::string& client,
                               const std::string& token) {
    std::string id_copy, status;
    {
        std::lock_guard<std::mutex> lk(mu_);
        std::ostringstream id;
        id << "room-" << (++counter_) << "-" << slugify(name);
        Room r;
        r.id = id.str();
        r.name = name.empty() ? "lobby" : name;
        Config cfg;
        r.state = std::make_shared<GameState>(cfg);
        Member m;
        m.client = client;
        m.token = token;
        r.members.push_back(m);
        auto [it, ok] = rooms_.emplace(r.id, std::move(r));
        Room* room = &it->second;
        id_copy = room->id;
        status = room->status_json();
    }
    // Signals are emitted OUTSIDE the lock: handlers call back into RoomManager.
    emit(EventType::RoomCreated, id_copy, status);
    emit(EventType::RoomStatusChanged, id_copy, status);
    return get_room(id_copy);
}

bool RoomManager::join_room(const std::string& room_id, const std::string& client,
                            const std::string& token) {
    std::string status;
    bool added = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        Room* r = find_locked(room_id);
        if (!r || r->is_full()) return false;
        for (auto& m : r->members)
            if (m.client == client) return true; // already in
        Member m;
        m.client = client;
        m.token = token;
        r->members.push_back(m);
        status = r->status_json();
        added = true;
    }
    if (added) {
        emit(EventType::PlayerJoined, room_id, status);
        emit(EventType::RoomStatusChanged, room_id, status);
    }
    return true;
}

bool RoomManager::leave_room(const std::string& room_id, const std::string& client) {
    std::lock_guard<std::mutex> lk(mu_);
    Room* r = find_locked(room_id);
    if (!r) return false;
    auto old = r->members.size();
    r->members.erase(std::remove_if(r->members.begin(), r->members.end(),
                                   [&](const Member& m) { return m.client == client; }),
                     r->members.end());
    if (r->members.size() == old) return false;
    std::string status = r->status_json();
    emit(EventType::PlayerLeft, r->id, status);
    emit(EventType::RoomStatusChanged, r->id, status);
    if (r->members.empty()) {
        std::string rid = r->id;
        rooms_.erase(rid);
        emit(EventType::RoomStatusChanged, rid, "{\"id\":\"" + net::json_escape(rid) + "\",\"closed\":true}");
    }
    return true;
}

bool RoomManager::set_identity(const std::string& room_id, const std::string& client,
                               const std::string& nickname, const std::string& char_class) {
    std::lock_guard<std::mutex> lk(mu_);
    Room* r = find_locked(room_id);
    if (!r) return false;
    bool valid_class = (char_class == "dwarf" || char_class == "goblin");
    for (auto& m : r->members) {
        if (m.client != client) continue;
        if (!nickname.empty()) m.nickname = nickname.substr(0, 24);
        if (valid_class) m.char_class = char_class;
        m.ready = !m.nickname.empty();
        std::string status = r->status_json();
        emit(EventType::RoomStatusChanged, r->id, status);
        return true;
    }
    return false;
}

std::string RoomManager::room_of_token(const std::string& token) {
    std::lock_guard<std::mutex> lk(mu_);
    if (token.empty()) return "";
    for (auto& [id, r] : rooms_)
        for (auto& m : r.members)
            if (m.token == token) return id;
    return "";
}

std::string RoomManager::room_of_client(const std::string& client) {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [id, r] : rooms_)
        for (auto& m : r.members)
            if (m.client == client) return id;
    return "";
}

bool RoomManager::list_rooms(std::vector<std::string>& out_ids) const {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [id, r] : rooms_) out_ids.push_back(id);
    return !rooms_.empty();
}

Room* RoomManager::get_room(const std::string& room_id) {
    std::lock_guard<std::mutex> lk(mu_);
    return find_locked(room_id);
}

void RoomManager::remove_room(const std::string& room_id) {
    std::lock_guard<std::mutex> lk(mu_);
    rooms_.erase(room_id);
    emit(EventType::RoomStatusChanged, room_id, "{\"id\":\"" + net::json_escape(room_id) + "\",\"closed\":true}");
}

std::vector<std::string> RoomManager::all_status_json() {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> out;
    for (auto& [id, r] : rooms_) out.push_back(r.status_json());
    return out;
}

} // namespace game
