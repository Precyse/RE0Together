#pragma once
// The co-op profile file (pure, unit tested; DD2 design progression.md 2): the guest's carried character as the engine's
// own JSON of each piece, read from the guest's own saves in solo play and spliced into the host's world at a co-op load.
//
//   dd2p 1
//   tdb <type database hash, hex>
//   level <n> job <n> equip <n>
//   <context|item|money> <arisen|pawn> <type name> <byte count>
//   <that many bytes of JSON>
//   ... (one block per piece)
//   end
//
// A profile from another type database is refused at the splice; a file without its `end` line is refused as cut short.
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace profile_rule {

constexpr uint32_t kFormat = 1;
constexpr size_t kMaxPieceBytes = 1 << 20;
constexpr size_t kMaxPieces = 8192;
constexpr int kHexBase = 16;

enum class Kind : uint8_t { Context, Item, Money };
enum class Owner : uint8_t { Arisen, MainPawn };

struct Piece {
    Kind kind = Kind::Context;
    Owner owner = Owner::Arisen;
    std::string type;  // the runtime type the JSON was made from
    std::string json;
};

struct Profile {
    uint32_t tdbHash = 0;
    int32_t level = 0, job = 0;
    int32_t equipIndex = 0;  // ItemManager.GameSaveData.ArisenEquipIndex
    std::vector<Piece> pieces;
};

inline const char* kindName(Kind kind) {
    switch (kind) {
        case Kind::Context: return "context";
        case Kind::Item: return "item";
        case Kind::Money: return "money";
    }
    return "";
}

inline const char* ownerName(Owner owner) { return owner == Owner::Arisen ? "arisen" : "pawn"; }

inline std::string encode(const Profile& profile) {
    char hash[9] = {};
    std::to_chars(hash, hash + 8, profile.tdbHash, kHexBase);
    std::string out = "dd2p " + std::to_string(kFormat) + "\ntdb " + hash + "\nlevel " + std::to_string(profile.level) + " job " +
                      std::to_string(profile.job) + " equip " + std::to_string(profile.equipIndex) + "\n";
    for (const Piece& piece : profile.pieces) {
        out += std::string(kindName(piece.kind)) + " " + ownerName(piece.owner) + " " + piece.type + " " +
               std::to_string(piece.json.size()) + "\n" + piece.json + "\n";
    }
    return out + "end\n";
}

namespace detail {

// Reads one line (without its newline) from `rest`; null at the end of the text.
inline std::optional<std::string_view> line(std::string_view& rest) {
    const size_t end = rest.find('\n');
    if (end == std::string_view::npos) return std::nullopt;
    const std::string_view out = rest.substr(0, end);
    rest.remove_prefix(end + 1);
    return out;
}

// The space-separated words of a line.
inline std::vector<std::string_view> words(std::string_view text) {
    std::vector<std::string_view> out;
    while (!text.empty()) {
        const size_t space = text.find(' ');
        out.push_back(text.substr(0, space));
        if (space == std::string_view::npos) break;
        text.remove_prefix(space + 1);
    }
    return out;
}

template <typename T>
bool number(std::string_view text, T& out, int base = 10) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), out, base);
    return error == std::errc() && end == text.data() + text.size() && !text.empty();
}

inline std::optional<Kind> kindOf(std::string_view word) {
    for (Kind kind : {Kind::Context, Kind::Item, Kind::Money}) {
        if (word == kindName(kind)) return kind;
    }
    return std::nullopt;
}

inline std::optional<Owner> ownerOf(std::string_view word) {
    if (word == ownerName(Owner::Arisen)) return Owner::Arisen;
    if (word == ownerName(Owner::MainPawn)) return Owner::MainPawn;
    return std::nullopt;
}

}  // namespace detail

inline std::optional<Profile> decode(std::string_view text) {
    using namespace detail;
    Profile out;
    const auto format = line(text), tdb = line(text), stats = line(text);
    if (!format || *format != "dd2p " + std::to_string(kFormat) || !tdb || tdb->substr(0, 4) != "tdb " ||
        !number(tdb->substr(4), out.tdbHash, kHexBase) || !stats) {
        return std::nullopt;
    }
    const auto stat = words(*stats);
    if (stat.size() != 6 || stat[0] != "level" || stat[2] != "job" || stat[4] != "equip" || !number(stat[1], out.level) ||
        !number(stat[3], out.job) || !number(stat[5], out.equipIndex)) {
        return std::nullopt;
    }
    while (const auto header = line(text)) {
        if (*header == "end") return text.empty() ? std::optional<Profile>(std::move(out)) : std::nullopt;
        const auto word = words(*header);
        size_t size = 0;
        if (word.size() != 4 || out.pieces.size() >= kMaxPieces || !number(word[3], size) || size > kMaxPieceBytes ||
            text.size() < size + 1 || text[size] != '\n') {
            return std::nullopt;
        }
        const auto kind = kindOf(word[0]);
        const auto owner = ownerOf(word[1]);
        if (!kind || !owner || word[2].empty()) return std::nullopt;
        out.pieces.push_back({*kind, *owner, std::string(word[2]), std::string(text.substr(0, size))});
        text.remove_prefix(size + 1);
    }
    return std::nullopt;
}

}  // namespace profile_rule
