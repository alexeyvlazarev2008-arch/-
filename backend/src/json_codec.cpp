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

static std::string find_key(const std::string& j, const std::string& k) {
    std::string pat = "\"" + k + "\"";
    size_t p = j.find(pat);
    if (p == std::string::npos) return "";
    p = j.find(':', p + pat.size());
    if (p == std::string::npos) return "";
    ++p;
    while (p < j.size() && std::isspace((unsigned char)j[p])) ++p;
    return std::to_string(p);  // encode position as string
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
