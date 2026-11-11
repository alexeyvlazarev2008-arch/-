// Гномья крепость — multiplayer game server (C++17, POSIX sockets).
// Protocol: newline-delimited JSON messages.
//   -> {"type":"create_room","name":"..."}        <- {"type":"room_created","id":"..."}
//   -> {"type":"list_rooms"}                      <- {"type":"rooms","ids":["..."]}
//   -> {"type":"join","room":"...","player":".."} <- {"type":"joined",...state}
//   -> {"type":"start","room":"..."}              <- {"type":"started",...state}
//   -> {"type":"move","room":"...","dx":1,"dy":0} <- state broadcast to room clients
//   -> {"type":"card","room":"...","card":"jump_2"}
//   -> {"type":"end_turn","room":"..."}
#include "game_state.hpp"
#include "json_codec.hpp"
#include "room_manager.hpp"
#include "tcp_server.hpp"
#include <csignal>
#include <cstdio>
#include <iostream>

int main(int argc, char** argv) {
    int port = argc > 1 ? std::atoi(argv[1]) : 9042;
    game::RoomManager rooms;
    net::TcpServer server(port);

    // Simple mapping client fd -> joined room id (single-threaded handler per client is fine
    // because RoomManager itself is mutex-protected).
    std::mutex map_mu;
    std::map<std::string, std::string> client_room;  // peer name -> room id

    server.set_handler([&](int fd, const std::string& peer, const std::string& line) -> std::string {
        using namespace net;
        std::string type = json_get_string(line, "type");
        std::string room_id;
        {
            std::lock_guard<std::mutex> lk(map_mu);
            auto it = client_room.find(peer);
            if (it != client_room.end()) room_id = it->second;
        }
        auto respond = [&](const std::string& body) { return body; };

        if (type == "create_room") {
            std::string name = json_get_string(line, "name");
            game::Room* r = rooms.create_room(name.empty() ? "lobby" : name);
            { std::lock_guard<std::mutex> lk(map_mu); client_room[peer] = r->id; }
            return "{\"type\":\"room_created\",\"id\":\"" + json_escape(r->id) + "\"}";
        }
        if (type == "list_rooms") {
            std::vector<std::string> ids;
            rooms.list_rooms(ids);
            std::string out = "{\"type\":\"rooms\",\"ids\":[";
            for (size_t i = 0; i < ids.size(); ++i) {
                if (i) out += ",";
                out += "\"" + json_escape(ids[i]) + "\"";
            }
            return out + "]}";
        }
        if (type == "join") {
            std::string rid = json_get_string(line, "room");
            std::string pname = json_get_string(line, "player");
            if (pname.empty()) pname = peer;
            if (!rooms.join_room(rid, pname))
                return "{\"type\":\"error\",\"msg\":\"cannot join\"}";
            { std::lock_guard<std::mutex> lk(map_mu); client_room[peer] = rid; }
            game::Room* r = rooms.get_room(rid);
            return "{\"type\":\"joined\",\"state\":" + r->state->to_json() + "}";
        }
        game::Room* r = room_id.empty() ? nullptr : rooms.get_room(room_id);
        if (!r) return "{\"type\":\"error\",\"msg\":\"no room\"}";

        if (type == "start") {
            if (!r->state->start_game()) return "{\"type\":\"error\",\"msg\":\"cannot start\"}";
        } else if (type == "move") {
            bool ok1, ok2;
            int dx = (int)json_get_int(line, "dx", ok1);
            int dy = (int)json_get_int(line, "dy", ok2);
            if (ok1 && ok2 && !r->state->move_current(dx, dy))
                return "{\"type\":\"error\",\"msg\":\"invalid move\"}";
        } else if (type == "card") {
            std::string card = json_get_string(line, "card");
            if (!r->state->use_card(card))
                return "{\"type\":\"error\",\"msg\":\"invalid card use\"}";
        } else if (type == "end_turn") {
            r->state->end_turn();
        } else if (type == "state") {
            return "{\"type\":\"state\",\"state\":" + r->state->to_json() + "}";
        } else {
            return "{\"type\":\"error\",\"msg\":\"unknown type\"}";
        }
        std::string state = "{\"type\":\"state\",\"state\":" + r->state->to_json() + "}";
        server.broadcast(state);
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
