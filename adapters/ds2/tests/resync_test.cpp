// resync: requests from peers and from the manual trigger are remembered per slot and scope, handed out once, and a
// malformed request is counted (no game). Also the shared "applying remote" flag.
#include <cstdio>
#include <cstring>
#include <thread>

#include "../src/remote_apply.h"
#include "../src/reject_counters.h"
#include "../src/resync.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

GameFrame request(uint8_t slot, uint32_t scopes) {
    GameFrame frame;
    frame.type = resync::kMsgResync;
    frame.slot = slot;
    frame.payload.resize(sizeof(scopes));
    std::memcpy(frame.payload.data(), &scopes, sizeof(scopes));
    return frame;
}

void requests() {
    resync::onFrame(request(1, resync::kFacts | resync::kAnim));
    resync::onFrame(request(2, resync::kFacts));
    check(resync::takeRequests(resync::kAuthority).empty(), "nobody asked for authority");
    const std::vector<uint8_t> facts = resync::takeRequests(resync::kFacts);
    check(facts.size() == 2 && facts[0] == 1 && facts[1] == 2, "both slots asked for facts");
    check(resync::takeRequests(resync::kFacts).empty(), "a request is handed out once");
    const std::vector<uint8_t> anim = resync::takeRequests(resync::kAnim);
    check(anim.size() == 1 && anim[0] == 1, "another scope of the same request is still waiting");

    resync::requestLocal(resync::kAll);
    resync::onFrame(request(1, resync::kAuthority));
    const std::vector<uint8_t> authority = resync::takeRequests(resync::kAuthority);
    check(authority.size() == 2 && authority[0] == proto::kSlotAll && authority[1] == 1, "a local request comes as slot all");
    check(resync::takeRequests(resync::kFacts).size() == 1, "a local request covers every scope");
}

void malformed() {
    resync::takeRequests(resync::kAll);
    reject_counters::reset();
    GameFrame shortFrame = request(1, resync::kAll);
    shortFrame.payload.pop_back();
    resync::onFrame(shortFrame);
    check(reject_counters::total(resync::kMsgResync, reject_counters::Reason::Malformed) == 1, "a short request is counted as malformed");
    resync::onFrame(request(1, 0xE0));
    check(resync::takeRequests(resync::kAll).empty(), "unknown scope bits ask for nothing");
    GameFrame other = request(1, resync::kAll);
    other.type = 0x0200;
    resync::onFrame(other);
    check(resync::takeRequests(resync::kAll).empty(), "another message type is ignored");
}

void applyingFlag() {
    check(!remote_apply::active(), "the flag starts off");
    {
        remote_apply::Scope outer;
        check(remote_apply::active(), "a scope turns it on");
        {
            remote_apply::Scope inner;
        }
        check(remote_apply::active(), "an inner scope ending leaves the outer one on");
        bool otherThread = true;
        std::thread([&otherThread] { otherThread = remote_apply::active(); }).join();
        check(!otherThread, "another thread is not affected");
    }
    check(!remote_apply::active(), "it is off once every scope ended");
}

}  // namespace

int main() {
    requests();
    malformed();
    applyingFlag();
    std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
    return g_failures ? 1 : 0;
}
