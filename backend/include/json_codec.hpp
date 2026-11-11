#pragma once
#include <string>
namespace net {
// Minimal JSON helpers sufficient for our line-protocol messages.
std::string json_escape(const std::string& s);
// Extract string value for key "k" from flat json object. Returns "" if absent.
std::string json_get_string(const std::string& j, const std::string& k);
// Extract integer value for key "k". Sets ok flag.
long long json_get_int(const std::string& j, const std::string& k, bool& ok);
} // namespace net
