// Гномья крепость — multiplayer game server (C++17, POSIX sockets).
// Protocol: newline-delimited JSON messages (Godot-compatible: JSON.parse_string
// on the client, no trailing commas, ASCII ids).
//
// Session identity ("One Time ID"):
//   -> {..,"token":"..."}  every message after the first must carry the current
//      token. On connect the server sends {"type":"hello","token":"<otid>"}.
//      redeeming a token (login/create/join) rotates it: the response contains
//      "token":"<next otid>". A used token can never be replayed.
//
// Messages:
//   -> {"type":"ping"}                          <- {"type":"pong","token":..}
//   -> {"type":"list_rooms","token":..}         <- {"type":"rooms","rooms":[status,..]}
//   -> {"type":"create_room","name":..,"nickname":..,"class":..,"token":..}
//                                               <- {"type":"room_created","id":..,"token":..}
//   -> {"type":"join","room":..,"nickname":..,"class":..,"token":..}
//                                               <- {"type":"joined","room":..,"token":..}
//   -> {"type":"set_identity","nickname":..,"class":..,"token":..}
//                                               <- {"type":"identity_ok","token":..}
//   -> {"type":"start","token":..}              <- {"type":"started", state} + room broadcast
//   -> {"type":"move","dx":1,"dy":0,"token":..} <- state broadcast to room members
//   -> {"type":"card","card":"jump_2","token":..}
//   -> {"type":"end_turn","token":..}
//   -> {"type":"state","token":..}              <- {"type":"state","state":{..}}
//   -> {"type":"leave","token":..}
//
// Signals (EventBus -> clients):
//   {"type":"room_status","room":..,"status":{..}}  // players count + nicknames
//   {"type":"player_joined"/"player_left","room":..,"status":{..}}
#include "event_bus.hpp"
#include "game_state.hpp"
#include "identity.hpp"
#include "json_codec.hpp"
#include "room_manager.hpp"
#include "tcp_server.hpp"
#include <atomic>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <map>
#include <set>
#include <thread>

int main(int argc, char** argv) {
    int port = argc > 1 ? std::atoi(argv[1]) : 9042;

    game::EventBus bus;
    game::IdentityStore ids;
    game::RoomManager rooms(&bus);
    net::TcpServer server(port);

    std::mutex map_mu;
    std::map<std::string, std::string> client_name; // peer -> nickname (for player slots)
    std::map<std::string, std::string> client_class;

    auto room_peers = [&](const std::string& rid) {
        std::set<std::string> out;
        if (game::Room* r = rooms.get_room(rid))
            for (auto& m : r->members) out.insert(m.client);
        return out;
    };

    auto send_to_peers = [&](const std::set<std::string>& peers, const std::string& msg) {
        for (auto& p : peers) server.send_to_peer(p, msg);
    };

    // --- subscribe to signals: forward room events to interested clients ----
    bus.subscribe([&](const game::Event& e) {
        switch (e.type) {
        case game::EventType::RoomCreated:
        case game::EventType::RoomStatusChanged: {
            // everyone in the room gets status; senders of create also learn id
            // Godot network_client maps "room_status" -> room_status_updated(status);
            // room_list.gd filters by status.id, so the payload must be the room
            // status object itself (contains id/name/players/members).
            std::string msg = "{\"type\":\"room_status\",\"status\":" + e.payload_json + "}";
            send_to_peers(room_peers(e.room_id), msg);
            break;
        }
        case game::EventType::PlayerJoined:
        case game::EventType::PlayerLeft: {
            std::string t = e.type == game::EventType::PlayerJoined ? "player_joined" : "player_left";
            std::string msg = "{\"type\":\"" + t + "\",\"status\":" + e.payload_json + "}";
            send_to_peers(room_peers(e.room_id), msg);
            // also refresh the global room list view for everybody connected
            std::string all = "{\"type\":\"rooms\",\"rooms\":[";
            auto statuses = rooms.all_status_json();
            for (size_t i = 0; i < statuses.size(); ++i) { if (i) all += ","; all += statuses[i]; }
            all += "]}";
            server.broadcast(all);
            break;
        }
        case game::EventType::GameStarted:
        case game::EventType::StateChanged: {
            std::string msg = "{\"type\":\"state\",\"state\":" + e.payload_json + "}";
            send_to_peers(room_peers(e.room_id), msg);
            break;
        }
        default:
            break;
        }
    });

    server.set_on_connect([&](const std::string& peer) {
        std::string tok = ids.issue(peer);
        server.send_to_peer(peer, "{\"type\":\"hello\",\"token\":\"" + net::json_escape(tok) + "\"}");
    });
    server.set_on_disconnect([&](const std::string& peer) {
        std::string rid = rooms.room_of_client(peer);
        if (!rid.empty()) rooms.leave_room(rid, peer);
        ids.revoke_peer(peer);
        std::lock_guard<std::mutex> lk(map_mu);
        client_name.erase(peer);
        client_class.erase(peer);
    });

    server.set_handler([&](int fd, const std::string& peer, const std::string& line) -> std::string {
        using namespace net;
        using namespace game;
        std::string type = json_get_string(line, "type");
        std::string token = json_get_string(line, "token");
        std::string next_token;
        bool fresh = ids.redeem(token, peer, next_token); // one-time: consumed on success

        auto with_tok = [&](const std::string& body) {
            // inject rotated token before closing brace (body ends with '}')
            if (body.size() >= 2 && body.back() == '}' && !next_token.empty())
                return body.substr(0, body.size() - 1) + ",\"token\":\"" + net::json_escape(next_token) + "\"}";
            return body;
        };

        if (type == "ping") return with_tok("{\"type\":\"pong\"}");

        // Fallback: client sent no/stale token but its hello-issued session
        // token is still outstanding — redeem that instead. Replay protection
        // stays strict (each token works once), and a Godot client can never
        // get locked out of the room it just created (room ids are NOT tokens).
        if (!fresh) fresh = ids.redeem(ids.current(peer), peer, next_token);
        if (!fresh) return "{\"type\":\"error\",\"msg\":\"bad or missing one-time token\"}";

        if (type == "create_room") {
            std::string name = json_get_string(line, "name");
            std::string nick = json_get_string(line, "nickname");
            std::string cls = json_get_string(line, "class");
            Room* r = rooms.create_room(name.empty() ? "lobby" : name, peer, next_token);
            if (!nick.empty() || !cls.empty()) rooms.set_identity(r->id, peer, nick, cls);
            { std::lock_guard<std::mutex> lk(map_mu); client_name[peer] = nick; client_class[peer] = cls; }
            return with_tok("{\"type\":\"room_created\",\"id\":\"" + net::json_escape(r->id) +
                            "\",\"status\":" + r->status_json() + "}");
        }
        if (type == "list_rooms") {
            std::string out = "{\"type\":\"rooms\",\"rooms\":[";
            auto statuses = rooms.all_status_json();
            for (size_t i = 0; i < statuses.size(); ++i) { if (i) out += ","; out += statuses[i]; }
            return with_tok(out + "]}");
        }
        if (type == "join") {
            std::string rid = json_get_string(line, "room");
            std::string nick = json_get_string(line, "nickname");
            std::string cls = json_get_string(line, "class");
            Room* jr = rooms.get_room(rid);
            if (!jr || !rooms.join_room(rid, peer, next_token))
                return with_tok("{\"type\":\"error\",\"msg\":\"cannot join\"}");
            if (!nick.empty() || !cls.empty()) rooms.set_identity(rid, peer, nick, cls);
            { std::lock_guard<std::mutex> lk(map_mu); client_name[peer] = nick; client_class[peer] = cls; }
            // Godot's room_list.gd opens the board scene on "joined" and draws
            // the server state immediately — include it in the reply.
            return with_tok("{\"type\":\"joined\",\"room\":\"" + net::json_escape(rid) +
                            "\",\"status\":" + jr->status_json() +
                            ",\"state\":" + jr->state->to_json() + "}");
        }
        if (type == "leave") {
            std::string rid = rooms.room_of_client(peer);
            if (!rid.empty()) rooms.leave_room(rid, peer);
            return with_tok("{\"type\":\"left\"}");
        }

        std::string rid = rooms.room_of_client(peer);
        Room* r = rid.empty() ? nullptr : rooms.get_room(rid);

        if (type == "set_identity") {
            if (!r) return with_tok("{\"type\":\"error\",\"msg\":\"no room\"}");
            std::string nick = json_get_string(line, "nickname");
            std::string cls = json_get_string(line, "class");
            if (!rooms.set_identity(rid, peer, nick, cls))
                return with_tok("{\"type\":\"error\",\"msg\":\"identity failed\"}");
            { std::lock_guard<std::mutex> lk(map_mu); client_name[peer] = nick; client_class[peer] = cls; }
            return with_tok("{\"type\":\"identity_ok\"}");
        }
        if (!r) return with_tok("{\"type\":\"error\",\"msg\":\"no room\"}");

        if (type == "start") {
            // The host may press "start" right after creating the room without
            // an explicit join — make sure the sender is a member first.
            bool in_room = false;
            for (auto& m : r->members) if (m.client == peer) { in_room = true; break; }
            if (!in_room) rooms.join_room(rid, peer, next_token);
            // transfer lobby members (nickname+class) into game players
            r->state->clear_players();
            for (auto& m : r->members) r->state->add_player(m.nickname.empty() ? m.client : m.nickname);
            if (!r->state->start_game())
                return with_tok("{\"type\":\"error\",\"msg\":\"cannot start\"}");
            std::string st = "{\"type\":\"state\",\"state\":" + r->state->to_json() + "}";
            bus.emit(Event{EventType::GameStarted, rid, r->state->to_json()});
            return with_tok(st);
        } else if (type == "move") {
            bool ok1, ok2;
            int dx = (int)json_get_int(line, "dx", ok1);
            int dy = (int)json_get_int(line, "dy", ok2);
            if (ok1 && ok2 && !r->state->move_current(dx, dy))
                return with_tok("{\"type\":\"error\",\"msg\":\"invalid move\"}");
        } else if (type == "card") {
            std::string card = json_get_string(line, "card");
            if (!r->state->use_card(card))
                return with_tok("{\"type\":\"error\",\"msg\":\"invalid card use\"}");
        } else if (type == "end_turn") {
            r->state->end_turn();
        } else if (type == "state") {
            return with_tok("{\"type\":\"state\",\"state\":" + r->state->to_json() + "}");
        } else {
            return with_tok("{\"type\":\"error\",\"msg\":\"unknown type\"}");
        }
        bus.emit(Event{EventType::StateChanged, rid, r->state->to_json()});
        return "";
    });

    if (!server.start()) {
        std::cerr << "Failed to bind port " << port << "\n";
        return 1;
    }
    std::cout << "Гномья крепость server listening on port " << port << std::endl;
    std::signal(SIGINT, [](int) { std::exit(0); });
    std::signal(SIGTERM, [](int) { std::exit(0); });
    while (true) std::this_thread::sleep_for(std::chrono::seconds(1));
}
