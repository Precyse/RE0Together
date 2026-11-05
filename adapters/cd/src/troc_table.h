#pragma once
// The Troc message catalogue as data (pure, unit tested): name <-> opcode and each message's limits. Filled at run time
// by troc_catalogue.cpp from the game's own static Troc instances; opcodes are never constants in our code because
// they shift between game patches.
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace troc_table {

constexpr uint32_t kOpcodeCount = 0x10000;

struct Info {
    std::string name;  // without the class prefix: "MoveActorReq"
    uint16_t opcode = 0;
    uint8_t kind = 0;
    uint8_t channel = 0;
    uint16_t flags = 0;
    uint16_t minSize = 0;  // whole packet, header included
    uint16_t maxSize = 0;
    uint8_t deferred = 0;
    uint8_t disabled = 0;
};

// "MoveActorReq" from the RTTI type name ".?AVTrocTrMoveActorReq@pa@@"; empty when the name is not a pa::TrocTr class.
inline std::string_view messageName(std::string_view decorated) {
    constexpr std::string_view kPrefix = ".?AVTrocTr";
    constexpr std::string_view kSuffix = "@pa@@";
    if (decorated.size() <= kPrefix.size() + kSuffix.size()) return {};
    if (decorated.substr(0, kPrefix.size()) != kPrefix) return {};
    if (decorated.substr(decorated.size() - kSuffix.size()) != kSuffix) return {};
    return decorated.substr(kPrefix.size(), decorated.size() - kPrefix.size() - kSuffix.size());
}

class Table {
public:
    // False (and nothing stored) when the opcode or the name is already taken.
    bool add(Info info) {
        if (byOpcode_.count(info.opcode) || byName_.count(info.name)) return false;
        byName_.emplace(info.name, info.opcode);
        byOpcode_.emplace(info.opcode, std::move(info));
        return true;
    }

    const Info* find(uint16_t opcode) const {
        const auto it = byOpcode_.find(opcode);
        return it == byOpcode_.end() ? nullptr : &it->second;
    }

    std::optional<uint16_t> opcodeOf(std::string_view name) const {
        const auto it = byName_.find(std::string(name));
        if (it == byName_.end()) return std::nullopt;
        return it->second;
    }

    // The names of `required` that the table does not hold.
    template <size_t N>
    std::vector<std::string_view> missing(const std::array<std::string_view, N>& required) const {
        std::vector<std::string_view> out;
        for (std::string_view name : required) {
            if (!opcodeOf(name)) out.push_back(name);
        }
        return out;
    }

    size_t size() const { return byOpcode_.size(); }
    const std::unordered_map<uint16_t, Info>& all() const { return byOpcode_; }

private:
    std::unordered_map<uint16_t, Info> byOpcode_;
    std::unordered_map<std::string, uint16_t> byName_;
};

}  // namespace troc_table
