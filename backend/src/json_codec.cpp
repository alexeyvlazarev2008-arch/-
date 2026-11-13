#include "json_codec.hpp"
#include <cctype>

namespace net {

std::string json_escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            default: o += c;
        }
    }
    return o;
}

// Find the value position of a top-level key. Two correctness fixes:
//  1. Keys are matched only OUTSIDE string values (previously searching for
//     "room" matched the substring inside the VALUE "room-1-slug", so the
//     server read the room id as the room to join -> every join failed).
//  2. The character after the closing quote must be ':' (so searching "name"
//     does not match the prefix of "nickname").
static std::string find_key(const std::string& j, const std::string& k) {
    std::string pat = "\"" + k + "\"";
    bool in_str = false;
    for (size_t i = 0; i < j.size(); ++i) {
        char c = j[i];
        if (in_str) {
            if (c == '\\') ++i;
            else if (c == '"') in_str = false;
            continue;
        }
        if (c == '"') {
            if (j.compare(i, pat.size(), pat) == 0) {
                size_t after = i + pat.size();
                while (after < j.size() && std::isspace((unsigned char)j[after])) ++after;
                if (after < j.size() && j[after] == ':') {
                    ++after;
                    while (after < j.size() && std::isspace((unsigned char)j[after])) ++after;
                    return std::to_string(after);  // encode position as string
                }
            }
            in_str = true;
        }
    }
    return "";
}

std::string json_get_string(const std::string& j, const std::string& k) {
    std::string pos = find_key(j, k);
    if (pos.empty()) return "";
    size_t p = std::stoul(pos);
    if (p >= j.size() || j[p] != '"') return "";
    ++p;
    std::string out;
    while (p < j.size() && j[p] != '"') {
        if (j[p] == '\\' && p + 1 < j.size()) ++p;
        out += j[p++];
    }
    return out;
}

long long json_get_int(const std::string& j, const std::string& k, bool& ok) {
    ok = false;
    std::string pos = find_key(j, k);
    if (pos.empty()) return 0;
    size_t p = std::stoul(pos);
    std::string num;
    if (p < j.size() && (j[p] == '-' || j[p] == '+')) num += j[p++];
    while (p < j.size() && std::isdigit((unsigned char)j[p])) num += j[p++];
    if (num.empty() || num == "-") return 0;
    ok = true;
    return std::stoll(num);
}

} // namespace net
