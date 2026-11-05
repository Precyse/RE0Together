#pragma once
// Funnel packets to coop\capture-<stamp>.cdcap (adapter.ini capture=1), for probe L01 and the golden packets of
// size_table.h. Decoded by tools/cd/capture_dump.py with coop\troc_catalogue.tsv.
//
// File: CaptureFileHeader, then records: CaptureRecord followed by `size` packet bytes. Integers little-endian.
#include <cstdint>

#include "packet.h"

namespace capture {

constexpr char kMagic[8] = {'C', 'D', 'C', 'A', 'P', '1', 0, 0};

#pragma pack(push, 1)
struct FileHeader {
    char magic[8];
    uint32_t exeTimeDateStamp;
    uint32_t reserved;
    uint64_t qpcFrequency;
};
struct Record {
    uint64_t qpc;
    uint32_t threadId;
    uint16_t size;      // whole packet, header included
    uint8_t direction;  // packet::Direction
    uint8_t session;    // PostPacket's session index (4 = server); 0 for received packets
    uint32_t channel;
    uint32_t flags;
};
#pragma pack(pop)
static_assert(sizeof(FileHeader) == 24);
static_assert(sizeof(Record) == 24);

// Opens the file and starts the writer thread; false when the file cannot be created.
bool start();

// Any thread: appends one packet (dropped and counted when the writer falls behind).
void record(packet::Direction direction, uint64_t session, uint64_t channel, uint64_t flags, const uint8_t* bytes,
            uint32_t size);

uint64_t dropped();

}  // namespace capture
