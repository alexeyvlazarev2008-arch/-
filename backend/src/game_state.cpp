#include "game_state.hpp"
#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace game {

static Cell char_to_cell(char c) {
    switch (c) {
        case '#': return Cell::Wall;
        case 'o': return Cell::Stone;
        case 'g': return Cell::Gem;
        case 'x': return Cell::Box;
        case 'm': return Cell::Mushroom;
        case 'c': return Cell::Crystal;
        case 'Y': return Cell::YellowSpawn;
        case 'G': return Cell::GreenSpawn;
        default:  return Cell::Empty;
    }
}
static const char* cell_to_char(Cell c) {
    switch (c) {
        case Cell::Wall: return "#";
        case Cell::Stone: return "o";
        case Cell::Gem: return "g";
        case Cell::Box: return "x";
        case Cell::Mushroom: return "m";
        case Cell::Crystal: return "c";
        case Cell::YellowSpawn: return "Y";
        case Cell::GreenSpawn: return "G";
        default: return ".";
    }
}

// Predefined mine maps with exits on all four sides (concept from issue #2).
// One map per supported board size. '.' floor, '#' wall, 'o' stone, 'g' gem,
// 'x' box, 'm' mushroom, 'c' crystal, 'Y' yellow spawn, 'G' green spawn.
static const char* MAP_6[] = {
    "..#..Y",
    ".o#.#.",
    "#..g..",
    "..#.o#",
    "G#.#..",
    "..#...",
};
static const char* MAP_8[] = {
    "...##..Y",
    ".o..##..",
    "#..g...#",
    "..#x.m..",
    "..c.#...",
    "#..o..##",
    "G....#..",
    "..##...o",
};
static const char* MAP_10[] = {
    "....##....",
    ".o...##.Y.",
    "..#...g...",
    "#..x.#..o#",
    "..c..#....",
    "....#..m..",
    "#.o..##...",
    "...g...#.G",
    ".##..o....",
    "....##...o",
};

GameState::GameState(Config cfg) : cfg_(cfg) {
    if (cfg_.size != 6 && cfg_.size != 8 && cfg_.size != 10) cfg_.size = 8;
    const char** map = cfg_.size == 6 ? MAP_6 : (cfg_.size == 8 ? MAP_8 : MAP_10);
    grid_.resize(cfg_.size * cfg_.size);
    for (int y = 0; y < cfg_.size; ++y)
        for (int x = 0; x < cfg_.size; ++x)
            grid_[y * cfg_.size + x] = static_cast<uint8_t>(char_to_cell(map[y][x]));

    // Lasers placed along walls pointing into the mine.
    lasers_.push_back({1, 0, 0, 1, LaserColor::Red});     // red near top-left
    lasers_.push_back({cfg_.size - 2, cfg_.size - 1, 0, -1, LaserColor::Green}); // green bottom
    lasers_.push_back({0, cfg_.size / 2, 1, 0, LaserColor::Yellow});             // yellow mid-left
}

bool GameState::in_bounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < cfg_.size && y < cfg_.size;
}
Cell GameState::cell_at(int x, int y) const {
    if (!in_bounds(x, y)) return Cell::Wall;
    return static_cast<Cell>(grid_[y * cfg_.size + x]);
}
void GameState::set_cell(int x, int y, Cell c) { grid_[y * cfg_.size + x] = static_cast<uint8_t>(c); }

int GameState::player_index_at(int x, int y) const {
    for (size_t i = 0; i < players_.size(); ++i)
        if (players_[i].alive && players_[i].x == x && players_[i].y == y) return (int)i;
    return -1;
}
int GameState::goblin_index_at(int x, int y) const {
    for (size_t i = 0; i < goblins_.size(); ++i)
        if (goblins_[i].x == x && goblins_[i].y == y) return (int)i;
    return -1;
}
const Player* GameState::player_at(int x, int y) const {
    int i = player_index_at(x, y);
    return i < 0 ? nullptr : &players_[i];
}
const Player& GameState::current_player() const { return players_[current_]; }

bool GameState::lit_by(int x, int y, LaserColor color) const {
    for (const auto& l : lasers_) {
        if (l.color != color) continue;
        int cx = l.x, cy = l.y;
        while (in_bounds(cx, cy)) {
            if (cx == x && cy == y) return true;
            if (cell_at(cx, cy) == Cell::Wall && !(cx == l.x && cy == l.y)) break; // beam blocked by walls
            cx += l.dx; cy += l.dy;
        }
    }
    return false;
}

bool GameState::can_walk(Cell c, const Player& p) const {
    if (c == Cell::Empty || c == Cell::YellowSpawn || c == Cell::GreenSpawn) return true;
    // Green laser lets a player pass through walls.
    if (c == Cell::Wall && lit_by(p.x, p.y, LaserColor::Green)) return true;
    return false;
}

bool GameState::add_player(const std::string& name) {
    if (phase_ != Phase::Lobby) return false;
    if ((int)players_.size() >= cfg_.max_players) return false;
    Player p;
    p.id = (int)players_.size();
    p.name = name;
    // Spawn corners: (0,0), (S-1,S-1), (0,S-1), (S-1,0) — find free one.
    int S = cfg_.size;
    const int corners[4][2] = {{0,0},{S-1,S-1},{0,S-1},{S-1,0}};
    for (auto& cn : corners)
        if (player_index_at(cn[0], cn[1]) < 0 && can_walk(cell_at(cn[0], cn[1]), p)) {
            p.x = cn[0]; p.y = cn[1]; break;
        }
    players_.push_back(p);
    return true;
}

bool GameState::start_game() {
    if (players_.empty()) return false;
    phase_ = Phase::Playing;
    turn_ = 1;
    current_ = 0;
    moves_left_ = cfg_.moves_per_turn;
    cards_left_ = 1;
    return true;
}

// Push object located at (fx,fy) towards (tx,ty). Returns success of the whole action.
bool GameState::push_from(int fx, int fy, int tx, int ty) {
    Cell src = cell_at(fx, fy);
    bool dx = lit_by(fx, fy, LaserColor::Red);   // red laser allows breaking m/c
    bool dy_ = lit_by(fx, fy, LaserColor::Yellow); // yellow laser breaks stones & kills players
    bool pushable = (src == Cell::Stone || src == Cell::Gem || src == Cell::Box ||
                     ((src == Cell::Mushroom || src == Cell::Crystal) && dx));
    if (!pushable) return false;

    // Yellow laser destroys the pushed stone entirely instead of moving it.
    if (src == Cell::Stone && dy_) {
        set_cell(fx, fy, Cell::Empty);
        total_mined_++;
        players_[current_].contribution++;
        return true;
    }

    if (!in_bounds(tx, ty)) {
        // Pushed off the board edge: stone/gem is mined.
        if (src == Cell::Stone) { total_mined_++; players_[current_].contribution++; }
        else if (src == Cell::Gem) { players_[current_].stones_held++; players_[current_].contribution++; }
        set_cell(fx, fy, Cell::Empty);
        return true;
    }
    Cell dst = cell_at(tx, ty);
    if (dst != Cell::Empty && dst != Cell::YellowSpawn && dst != Cell::GreenSpawn) return false;
    if (player_index_at(tx, ty) >= 0) {
        // Yellow laser lets you push a stone into a player, killing them.
        if (!dy_) return false;
        kill_player(player_index_at(tx, ty));
    }
    set_cell(tx, ty, src);
    set_cell(fx, fy, Cell::Empty);
    return true;
}

void GameState::kill_player(int pidx) {
    Player& p = players_[pidx];
    p.alive = false;
    // Respawn in any of the 4 corners (README rule).
    int S = cfg_.size;
    const int corners[4][2] = {{0,0},{S-1,S-1},{0,S-1},{S-1,0}};
    for (auto& cn : corners) {
        if (in_bounds(cn[0], cn[1]) && cell_at(cn[0], cn[1]) == Cell::Empty &&
            player_index_at(cn[0], cn[1]) < 0) {
            p.x = cn[0]; p.y = cn[1]; p.alive = true; return;
        }
    }
}

void GameState::spawn_goblins() {
    for (int y = 0; y < cfg_.size; ++y)
        for (int x = 0; x < cfg_.size; ++x)
            if (cell_at(x, y) == Cell::GreenSpawn && goblin_index_at(x, y) < 0 &&
                player_index_at(x, y) < 0 && (turn_ % 3 == 0 || goblins_.empty())) {
                goblins_.push_back({x, y});
            }
}

void GameState::check_goblins_adjacent() {
    // Goblins kill a player standing on any of the 4 neighbouring cells (README).
    const Player& p = players_[current_];
    if (!p.alive) return;
    for (const auto& g : goblins_) {
        if (std::abs(g.x - p.x) + std::abs(g.y - p.y) == 1) {
            kill_player(current_);
            return;
        }
    }
}

bool GameState::move_current(int dx, int dy) {
    if (phase_ != Phase::Playing) return false;
    if (moves_left_ <= 0) return false;
    if (std::abs(dx) + std::abs(dy) != 1) return false;
    Player& p = players_[current_];
    if (!p.alive) return false;

    int nx = p.x + dx, ny = p.y + dy;
    // Pushing an object directly ahead.
    Cell ahead = cell_at(p.x + dx, p.y + dy);
    if (ahead == Cell::Stone || ahead == Cell::Gem || ahead == Cell::Box ||
        ahead == Cell::Mushroom || ahead == Cell::Crystal) {
        if (!push_from(p.x + dx, p.y + dy, p.x + 2*dx, p.y + 2*dy)) return false;
        // After pushing, player may step forward only if target walkable and not occupied
        // Sokoban-style: push consumes the move but player stays.
        moves_left_--;
        check_goblins_adjacent();
        return true;
    }
    if (!in_bounds(nx, ny)) return false;
    if (goblin_index_at(nx, ny) >= 0) return false;
    if (player_index_at(nx, ny) >= 0) return false;
    if (!can_walk(ahead, p)) return false;

    p.x = nx; p.y = ny;
    moves_left_--;

    // Standing where a green-laser beam no longer covers while inside a wall => death.
    if (ahead == Cell::Wall && !lit_by(nx, ny, LaserColor::Green)) {
        kill_player(current_);
    }
    // Adjacent goblin kills the player.
    check_goblins_adjacent();
    return true;
}

bool GameState::use_card(const std::string& card_id) {
    if (phase_ != Phase::Playing) return false;
    if (cards_left_ <= 0) return false;
    // Card system is defined in frontend/cards/tags.md; backend validates usage count only.
    cards_left_--;
    return true;
}

bool GameState::end_turn() {
    if (phase_ != Phase::Playing) return false;
    // Advance to next alive player.
    for (int i = 1; i <= (int)players_.size(); ++i) {
        int idx = (current_ + i) % (int)players_.size();
        if (players_[idx].alive) { current_ = idx; break; }
    }
    if (current_ == 0 || turn_ >= cfg_.total_turns) {
        // full round completed -> new global turn
    }
    if (++round_count_ % (int)players_.size() == 0) {
        turn_++;
        if (turn_ >= cfg_.goblin_start_turn) spawn_goblins();
        // goblins move one step toward nearest player each turn would go here (AI TBD)
    }
    moves_left_ = cfg_.moves_per_turn;
    cards_left_ = 1;
    if (total_mined_ >= cfg_.stones_to_build) phase_ = Phase::Finished; // fortress built!
    else if (turn_ > cfg_.total_turns) phase_ = Phase::Finished;        // invasion, everyone loses
    return true;
}

std::string GameState::to_json() const {
    std::ostringstream o;
    // NOTE: player names may contain non-ASCII (Cyrillic) or quotes — escape them.
    auto esc = [](const std::string& s) {
        std::string out;
        for (unsigned char c : s) {
            if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back(c); }
            else if (c == '\n') out += "\\n";
            else out.push_back((char)c);
        }
        return out;
    };
    o << "{\"phase\":" << (int)phase_
      << ",\"turn\":" << turn_
      << ",\"mined\":" << total_mined_
      << ",\"size\":" << cfg_.size
      << ",\"moves_left\":" << moves_left_
      << ",\"cards_left\":" << cards_left_
      << ",\"current\":" << current_
      << ",\"grid\":\"";
    for (int y = 0; y < cfg_.size; ++y)
        for (int x = 0; x < cfg_.size; ++x)
            o << cell_to_char(cell_at(x, y));
    o << "\",\"players\":[";
    for (size_t i = 0; i < players_.size(); ++i) {
        const auto& p = players_[i];
        if (i) o << ",";
        o << "{\"id\":" << p.id << ",\"name\":\"" << esc(p.name) << "\",\"x\":" << p.x
          << ",\"y\":" << p.y << ",\"alive\":" << (p.alive ? "true" : "false")
          << ",\"gems\":" << p.stones_held << ",\"score\":" << p.contribution << "}";
    }
    o << "],\"goblins\":[";
    for (size_t i = 0; i < goblins_.size(); ++i) {
        if (i) o << ",";
        o << "{\"x\":" << goblins_[i].x << ",\"y\":" << goblins_[i].y << "}";
    }
    o << "]}";
    return o.str();
}

} // namespace game
