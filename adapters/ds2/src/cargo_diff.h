#pragma once
// Pure comparison of two multisets of cargo pieces (no game): a piece is told apart by its kind and its order id (0 for plain
// cargo), since two pieces of one order never share an id. rack_sync brings the partner body's rack to the partner's list with
// it, locker sync brings a locker to the host's list.
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace cargo_diff {

using PieceKey = std::pair<uint32_t, uint64_t>;  // kind, order id
using Counts = std::map<PieceKey, size_t>;
using Handles = std::map<PieceKey, std::vector<uint64_t>>;

// Any piece type with `type` and `orderId` members.
template <class Piece>
PieceKey keyOf(const Piece& piece) {
    return {piece.type, piece.orderId};
}

template <class Piece>
Counts countsOf(const std::vector<Piece>& pieces) {
    Counts out;
    for (const Piece& piece : pieces) ++out[keyOf(piece)];
    return out;
}

// Pieces already grouped by key (handles, or whole pieces); empty groups count nothing.
template <class Item>
Counts countsOf(const std::map<PieceKey, std::vector<Item>>& grouped) {
    Counts out;
    for (const auto& [key, list] : grouped) {
        if (!list.empty()) out[key] = list.size();
    }
    return out;
}

// What makes two pieces the same in a locker: kind, order link, category and durability.
template <class Piece>
auto identity(const Piece& piece) {
    return std::tie(piece.type, piece.orderId, piece.secondId, piece.category, piece.durability);
}

// The pieces in one order, so two lists of the same pieces compare equal whatever order the engine walked them in.
template <class Piece>
void canonicalOrder(std::vector<Piece>& pieces) {
    std::sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) { return identity(a) < identity(b); });
}

// Whether two lists in canonical order hold the same pieces.
template <class Piece>
bool sameContents(const std::vector<Piece>& a, const std::vector<Piece>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const Piece& x, const Piece& y) { return identity(x) == identity(y); });
}

// The handles held beyond the wanted count of their key (all of them for a key that is not wanted), in key order.
inline std::vector<uint64_t> extra(const Handles& have, const Counts& want) {
    std::vector<uint64_t> out;
    for (const auto& [key, handles] : have) {
        const auto wanted = want.find(key);
        const size_t keep = wanted == want.end() ? 0 : wanted->second;
        for (size_t i = keep; i < handles.size(); ++i) out.push_back(handles[i]);
    }
    return out;
}

// How many more of each key are wanted than held (keys with nothing missing are left out).
inline Counts missing(const Counts& have, const Counts& want) {
    Counts out;
    for (const auto& [key, count] : want) {
        const auto held = have.find(key);
        const size_t heldCount = held == have.end() ? 0 : held->second;
        if (count > heldCount) out[key] = count - heldCount;
    }
    return out;
}

}  // namespace cargo_diff
