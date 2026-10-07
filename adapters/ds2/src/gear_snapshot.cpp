#include "gear_snapshot.h"

#include <cstdio>
#include <map>
#include <sstream>
#include <utility>

namespace gear_snapshot {

namespace {

constexpr const char* kHeader = "ds2-gear 1";
constexpr int kFieldsPerLine = 4;
constexpr unsigned kMaxByte = 255;

}  // namespace

Items minus(const Items& held, const Items& base) {
    std::map<std::pair<uint8_t, uint32_t>, int> owed;
    for (const Item& item : base) ++owed[{item.slot, item.type}];
    Items extra;
    for (const Item& item : held) {
        int& count = owed[{item.slot, item.type}];
        if (count > 0) {
            --count;
        } else {
            extra.push_back(item);
        }
    }
    return extra;
}

std::string format(const Items& items) {
    std::string text = std::string(kHeader) + "\n";
    for (const Item& item : items) {
        char line[96];
        std::snprintf(line, sizeof(line), "%u %u %u %.3f\n", item.slot, item.type, item.category, item.durability);
        text += line;
    }
    return text;
}

bool parse(const std::string& text, Items& items) {
    std::istringstream stream(text);
    std::string line;
    if (!std::getline(stream, line) || line != kHeader) return false;
    Items parsed;
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        unsigned slot = 0, type = 0, category = 0;
        float durability = 0;
        if (std::sscanf(line.c_str(), "%u %u %u %f", &slot, &type, &category, &durability) != kFieldsPerLine ||
            slot > kMaxByte || category > kMaxByte) {
            return false;
        }
        parsed.push_back({static_cast<uint8_t>(slot), type, static_cast<uint8_t>(category), durability});
    }
    items = std::move(parsed);
    return true;
}

}  // namespace gear_snapshot
