#pragma once
// Core game state for "Гномья крепость" (Dwarven Fortress board game backend).
// Mirrors the rules described in README.md:
//  - up to 4 players, shared goal: mine 25 stones and build the fortress in 20 turns
//  - each turn a player may move up to `moves_per_turn` steps; pushing is allowed
//  - a stone counts as mined when pushed off the board
#include <cstdint>
#include <string>
#include <vector>

namespace game {

enum class Cell : uint8_t {
    Empty = 0,     // floor
    Wall,          // impassable obstacle
    Stone,         // regular building stone (pushable, mineable)
    Gem,           // precious stone (pushable under red laser, tradeable)
    Box,           // crate (pushable)
    Mushroom,      // blocks movement; destructible by red-laser push
    Crystal,       // blocks movement; destructible by red-laser push
    YellowSpawn,   // yellow cell: stones spawn here
    GreenSpawn,    // green cell: goblins infiltrate here (after turn 12)
};

enum class LaserColor : uint8_t { Red = 0, Green = 1, Yellow = 2 };

struct Laser {
    int x = 0, y = 0;
    int dx = 0, dy = 1;      // direction the beam points
    LaserColor color = LaserColor::Red;
};

struct Player {
    int id = 0;
    std::string name;
    int x = 0, y = 0;
    int stones_held = 0;     // gems carried for trading
    bool alive = true;
    int contribution = 0;    // personal score (mined stones + built share)
};

struct Goblin {
    int x = 0, y = 0;
};

enum class Phase : uint8_t { Lobby = 0, Playing, Finished };

struct Config {
    int size = 8;              // board is square: 6, 8 or 10
    int max_players = 4;
    int moves_per_turn = 3;
    int total_turns = 20;
    int goblin_start_turn = 12;
    int stones_to_build = 25;
};

class GameState {
public:
    explicit GameState(Config cfg = {});

    // --- queries -------------------------------------------------------
    bool in_bounds(int x, int y) const;
    Cell cell_at(int x, int y) const;
    void set_cell(int x, int y, Cell c);
    int player_index_at(int x, int y) const;   // -1 if none
    int goblin_index_at(int x, int y) const;   // -1 if none
    const Player* player_at(int x, int y) const;
    const Player& current_player() const;
    int current_player_index() const { return current_; }
    int turn_number() const { return turn_; }
    int total_mined() const { return total_mined_; }
    Phase phase() const { return phase_; }
    const Config& config() const { return cfg_; }
    const std::vector<Player>& players() const { return players_; }
    const std::vector<Goblin>& goblins() const { return goblins_; }
    const std::vector<Laser>& lasers() const { return lasers_; }

    // --- mutations (return false on invalid action) --------------------
    bool add_player(const std::string& name);
    bool start_game();                       // lobby -> playing
    bool move_current(int dx, int dy);       // one step with push rules
    bool use_card(const std::string& card_id); // reserved for future card system
    bool end_turn();                         // advance to next player/turn

    // Serialize full state as compact JSON string (hand-rolled encoder).
    std::string to_json() const;

private:
    bool push_from(int fx, int fy, int tx, int ty); // push object at (fx,fy)->(tx,ty)
    void kill_player(int pidx);
    void check_goblins_adjacent();
    void spawn_goblins();
    bool can_walk(Cell c, const Player& p) const;
    bool lit_by(int x, int y, LaserColor color) const;

    Config cfg_;
    std::vector<uint8_t> grid_;   // size*size cells of Cell
    std::vector<Player> players_;
    std::vector<Goblin> goblins_;
    std::vector<Laser> lasers_;
    int current_ = 0;
    int turn_ = 0;                // global turn counter (increments per round)
    int round_count_ = 0;         // completed individual turns (for round detection)
    int moves_left_ = 0;
    int cards_left_ = 0;
    int total_mined_ = 0;
    Phase phase_ = Phase::Lobby;
};

} // namespace game
