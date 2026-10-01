// Replays partner packets through the adapter's real PadBuffer at 60 fps and reports how the input buffer copes:
// underruns (the partner stutters), skips (inputs dropped to catch up) and the delay the buffer adds.
//
// usage: trace_replay <coop\net_trace.bin> [--extra-jitter MS] [--loss PCT] [--seed N]
//        trace_replay --synthetic SECONDS [--latency MS] [--jitter MS] [--loss PCT] [--seed N]
// --extra-jitter / --loss make a recorded link worse, to see how much headroom the settings have.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "../src/net_trace.h"
#include "../src/pad_buffer.h"
#include "../src/pad_frame.h"
#include "../src/state_sync.h"

namespace {

constexpr double kFrameMs = 1000.0 / 60.0;
constexpr double kPercent = 100.0;
constexpr double kP95 = 0.95;

struct Packet {
    double arrivalMs;
    std::vector<uint32_t> frames;
};

struct Options {
    std::string trace;
    double syntheticSeconds = 0;
    double latencyMs = 40;
    double jitterMs = 0;
    double lossPercent = 0;
    unsigned seed = 1;
};

std::vector<double> g_stateArrivals;

bool readTrace(const std::string& path, std::vector<Packet>& packets) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    net_trace::TraceRecord header;
    std::vector<uint8_t> payload;
    while (std::fread(&header, sizeof(header), 1, file) == 1) {
        payload.resize(header.size);
        if (header.size && std::fread(payload.data(), 1, header.size, file) != header.size) break;
        if (header.type == state_sync::kMsgPlayerState) {
            g_stateArrivals.push_back(header.ms);
            continue;
        }
        if (header.type != pad::kMsgPadFrame || header.size < sizeof(uint32_t)) continue;
        pad::PadPacket packet{};
        std::memcpy(&packet, payload.data(), std::min(payload.size(), sizeof(packet)));
        Packet parsed{static_cast<double>(header.ms), {}};
        for (uint32_t i = 0; i < packet.count && i < pad::kFramesPerPacket; ++i) parsed.frames.push_back(packet.frames[i].frame);
        packets.push_back(parsed);
    }
    std::fclose(file);
    return true;
}

// A 60 fps sender whose packets carry the newest kFramesPerPacket frames, like input_record does.
std::vector<Packet> synthesize(const Options& options, std::mt19937& random) {
    std::vector<Packet> packets;
    const auto frames = static_cast<uint32_t>(options.syntheticSeconds * 1000.0 / kFrameMs);
    std::uniform_real_distribution<double> jitter(0.0, options.jitterMs);
    for (uint32_t frame = 0; frame < frames; ++frame) {
        Packet packet{frame * kFrameMs + options.latencyMs + jitter(random), {}};
        for (uint32_t back = std::min<uint32_t>(frame, pad::kFramesPerPacket - 1) + 1; back > 0; --back) {
            packet.frames.push_back(frame + 1 - back);
        }
        packets.push_back(packet);
    }
    return packets;
}

void degrade(std::vector<Packet>& packets, double extraJitterMs, double lossPercent, std::mt19937& random) {
    std::uniform_real_distribution<double> jitter(0.0, extraJitterMs);
    std::uniform_real_distribution<double> roll(0.0, kPercent);
    std::vector<Packet> kept;
    for (Packet& packet : packets) {
        if (roll(random) < lossPercent) continue;
        packet.arrivalMs += jitter(random);
        kept.push_back(packet);
    }
    std::stable_sort(kept.begin(), kept.end(), [](const Packet& a, const Packet& b) { return a.arrivalMs < b.arrivalMs; });
    packets.swap(kept);
}

void reportStateArrivals() {
    if (g_stateArrivals.size() < 2) return;
    std::vector<double> gaps;
    for (size_t i = 1; i < g_stateArrivals.size(); ++i) gaps.push_back(g_stateArrivals[i] - g_stateArrivals[i - 1]);
    std::sort(gaps.begin(), gaps.end());
    double sum = 0;
    for (double gap : gaps) sum += gap;
    std::printf("PLAYER_STATE gaps: mean %.1f ms, p95 %.1f ms, max %.1f ms (%zu packets)\n", sum / gaps.size(),
                gaps[static_cast<size_t>(gaps.size() * kP95)], gaps.back(), g_stateArrivals.size());
}

void replay(const std::vector<Packet>& packets) {
    PadBuffer<uint32_t> buffer;
    size_t next = 0;
    uint32_t newest = 0;
    size_t ticks = 0, consumed = 0, underruns = 0, skipped = 0, waiting = 0;
    double delayFrames = 0;
    const double end = packets.back().arrivalMs + kFrameMs;
    for (double now = packets.front().arrivalMs; now <= end; now += kFrameMs) {
        for (; next < packets.size() && packets[next].arrivalMs <= now; ++next) {
            for (uint32_t frame : packets[next].frames) {
                buffer.add(frame, frame);
                newest = std::max(newest, frame);
            }
        }
        uint32_t frame = 0;
        size_t skippedNow = 0;
        const auto step = buffer.advance(frame, skippedNow);
        ++ticks;
        skipped += skippedNow;
        if (step == PadBuffer<uint32_t>::Step::Underrun) ++underruns;
        if (step == PadBuffer<uint32_t>::Step::Waiting) ++waiting;
        if (step != PadBuffer<uint32_t>::Step::Consumed) continue;
        ++consumed;
        delayFrames += newest - frame;
    }
    std::printf("ticks %zu, replayed %zu, underruns %zu (%.2f%%), skipped %zu, waiting %zu\n", ticks, consumed, underruns,
                ticks ? kPercent * underruns / ticks : 0.0, skipped, waiting);
    std::printf("buffer delay: mean %.1f ms, final target %zu frames\n", consumed ? delayFrames / consumed * kFrameMs : 0.0,
                buffer.target());
}

bool parse(int argc, char** argv, Options& options, double& extraJitter) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--synthetic" && hasValue) options.syntheticSeconds = std::atof(argv[++i]);
        else if (arg == "--latency" && hasValue) options.latencyMs = std::atof(argv[++i]);
        else if (arg == "--jitter" && hasValue) options.jitterMs = std::atof(argv[++i]);
        else if (arg == "--extra-jitter" && hasValue) extraJitter = std::atof(argv[++i]);
        else if (arg == "--loss" && hasValue) options.lossPercent = std::atof(argv[++i]);
        else if (arg == "--seed" && hasValue) options.seed = static_cast<unsigned>(std::atoi(argv[++i]));
        else if (arg.rfind("--", 0) != 0) options.trace = arg;
        else return false;
    }
    return !options.trace.empty() || options.syntheticSeconds > 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    double extraJitter = 0;
    if (!parse(argc, argv, options, extraJitter)) {
        std::puts("usage: trace_replay <net_trace.bin> [--extra-jitter MS] [--loss PCT] [--seed N]\n"
                  "       trace_replay --synthetic SECONDS [--latency MS] [--jitter MS] [--loss PCT] [--seed N]");
        return 2;
    }
    std::mt19937 random(options.seed);
    std::vector<Packet> packets;
    if (options.syntheticSeconds > 0) {
        packets = synthesize(options, random);
    } else if (!readTrace(options.trace, packets)) {
        std::printf("cannot read %s\n", options.trace.c_str());
        return 1;
    }
    degrade(packets, extraJitter, options.lossPercent, random);
    if (packets.empty()) {
        std::puts("no PAD_FRAME packets");
        return 1;
    }
    std::printf("%zu pad packets\n", packets.size());
    replay(packets);
    reportStateArrivals();
    return 0;
}
