// fact_snapshot and the pending queue (no game): chunking of a large fact set, the chunk header checks, progress
// tracking, and held messages that are released when the object appears or expire and are counted.
#include <cstdio>
#include <vector>

#include "../src/fact_snapshot.h"
#include "../src/pending_queue.h"
#include "../src/reject_counters.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

std::vector<fact_wire::Entry> facts(size_t count) {
    std::vector<fact_wire::Entry> out(count);
    for (size_t i = 0; i < count; ++i) {
        out[i].kind = fact_wire::kKindInt;
        out[i].value = static_cast<uint32_t>(i);
        out[i].uuid[0] = static_cast<uint8_t>(i);
        out[i].uuid[1] = static_cast<uint8_t>(i >> 8);
    }
    return out;
}

void chunking() {
    const std::vector<std::vector<uint8_t>> none = fact_snapshot::encode(1, {});
    check(none.size() == 1, "an empty snapshot is still one chunk");

    const std::vector<fact_wire::Entry> big = facts(fact_wire::kMaxEntries * 2 + 5);
    const std::vector<std::vector<uint8_t>> chunks = fact_snapshot::encode(7, big);
    check(chunks.size() == 3, "2 x the entry limit plus 5 needs three chunks");

    std::vector<fact_wire::Entry> all;
    fact_snapshot::Progress progress;
    bool inOrder = true;
    fact_snapshot::Progress::State last = fact_snapshot::Progress::State::Partial;
    for (size_t i = 0; i < chunks.size(); ++i) {
        fact_snapshot::ChunkHeader header{};
        std::vector<fact_wire::Entry> part;
        if (!fact_snapshot::decode(chunks[i], header, part)) inOrder = false;
        inOrder = inOrder && header.snapshotId == 7 && header.index == i && header.count == 3;
        all.insert(all.end(), part.begin(), part.end());
        last = progress.onChunk(header);
    }
    check(inOrder, "every chunk decodes with its index and the snapshot id");
    check(all.size() == big.size() && all.back().value == big.back().value && all[300].value == 300, "all facts come back, in order");
    check(last == fact_snapshot::Progress::State::Complete, "the last chunk completes the snapshot");

    fact_snapshot::ChunkHeader header{};
    std::vector<fact_wire::Entry> part;
    fact_snapshot::decode(chunks[1], header, part);
    check(progress.onChunk(header) == fact_snapshot::Progress::State::Duplicate, "a repeated chunk is a duplicate");
    const fact_snapshot::ChunkHeader next{8, 0, 2};
    check(progress.onChunk(next) == fact_snapshot::Progress::State::Partial, "a newer snapshot starts over");
    check(progress.onChunk({8, 1, 2}) == fact_snapshot::Progress::State::Complete, "and completes on its own chunks");

    const std::vector<fact_wire::Entry> tooMany(static_cast<size_t>(fact_snapshot::kMaxChunks) * fact_wire::kMaxEntries + 1);
    check(fact_snapshot::encode(1, tooMany).empty(), "more facts than the chunks can number are refused");
}

void rejections() {
    const std::vector<uint8_t> chunk = fact_snapshot::encode(3, facts(2))[0];
    fact_snapshot::ChunkHeader header{};
    std::vector<fact_wire::Entry> part;
    check(fact_snapshot::decode(chunk, header, part) && part.size() == 2, "a good chunk decodes");
    check(!fact_snapshot::decode(std::span<const uint8_t>(chunk.data(), 5), header, part), "a chunk shorter than its header is rejected");
    check(!fact_snapshot::decode(std::span<const uint8_t>(chunk.data(), chunk.size() - 1), header, part) && part.empty(),
          "a truncated fact list is rejected");
    std::vector<uint8_t> outside = chunk;
    outside[4] = 9;  // index 9 of count 1
    check(!fact_snapshot::decode(outside, header, part), "a chunk index outside its snapshot is rejected");
    std::vector<uint8_t> empty = chunk;
    empty[6] = 0;
    empty[7] = 0;
    check(!fact_snapshot::decode(empty, header, part), "a snapshot of no chunks is rejected");
}

void pendingQueue() {
    reject_counters::reset();
    PendingQueue queue;
    const uint8_t payload[] = {1, 2, 3};
    constexpr uint16_t kType = 0x0108;
    check(queue.hold(10, kType, 1, payload, 1'000'000) && queue.hold(11, kType, 1, payload, 2'000'000) &&
              queue.hold(10, kType + 1, 2, payload, 3'000'000),
          "messages for objects not there yet are held");
    check(queue.size() == 3, "all three wait");
    const std::vector<PendingQueue::Message> first = queue.take(10);
    check(first.size() == 2 && first[0].type == kType && first[1].type == kType + 1 && first[0].payload.size() == 3 &&
              first[1].slot == 2,
          "an object that appears gets its messages, oldest first");
    check(queue.size() == 1 && queue.take(10).empty(), "they are removed once handed out");

    check(queue.expire(2'000'000 + PendingQueue::kHoldUs - 1) == 0, "nothing expires before the hold time");
    check(queue.expire(2'000'000 + PendingQueue::kHoldUs) == 1 && queue.size() == 0, "a message past ten seconds is dropped");
    check(reject_counters::total(kType, reject_counters::Reason::Expired) == 1, "and counted against its type");

    for (size_t i = 0; i < PendingQueue::kMaxHeld; ++i) queue.hold(i, kType, 1, payload, 5'000'000);
    check(!queue.hold(999'999, kType, 1, payload, 5'000'000) &&
              reject_counters::total(kType, reject_counters::Reason::Overflow) == 1,
          "a full queue refuses more and counts the overflow");
    queue.clear();
    check(queue.size() == 0, "clear empties it");
}

}  // namespace

int main() {
    chunking();
    rejections();
    pendingQueue();
    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
