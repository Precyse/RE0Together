// The guest's rules (no game): waiting, login, search, join, failed attempts, and the status lines.
#include <cstdio>

#include "../src/guest_join_rules.h"

namespace {

using guest_join_rules::Browser;
using guest_join_rules::Decision;
using guest_join_rules::Inputs;
using guest_join_rules::LanEntry;
using guest_join_rules::Machine;
using guest_join_rules::Step;

constexpr uint64_t kSecond = 1000;

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++g_failures;
}

Inputs ready(uint64_t seconds) {
    Inputs in;
    in.nowMs = seconds * kSecond;
    in.loggedIn = true;
    in.frontEndFree = true;
    return in;
}

bool is(const Decision& d, Step step, const char* state, const char* detail = "") {
    return d.step == step && d.status.state == state && d.status.detail == detail;
}

// Starts a query at second 0 and finds the host at second 1; the machine is then in its join attempt.
Decision startAndJoin(Machine& machine) {
    machine.update(ready(0));
    Inputs found = ready(1);
    found.browser = Browser::Querying;
    found.match = 2;
    return machine.update(found);
}


// The LAN path (steam transport): no login, the host's address is checked, the search is stopped after the dialog.
void lanPath() {
    constexpr uint32_t kHostAddress = 0x0A0B0C0D;
    constexpr uint32_t kRealAddress = 0xC0A80164;
    constexpr uint32_t kServerId = 7;
    const auto withHost = [](uint64_t seconds) {
        Inputs in = ready(seconds);
        in.hostAddress = kHostAddress;
        return in;
    };
    const auto searchingFor = [&](uint64_t seconds) {
        Inputs in = withHost(seconds);
        in.searchRunning = true;
        return in;
    };
    const auto listing = [&](uint64_t seconds, uint32_t address) {
        Inputs in = searchingFor(seconds);
        in.lanEntry = LanEntry{kServerId, address};
        in.lanListed = 1;
        return in;
    };

    Machine lan(guest_join_rules::Mode::Lan);
    check(is(lan.update(ready(0)), Step::None, "waiting", "link"), "LAN: without the link or the host member it waits");
    Inputs busy = withHost(0);
    busy.frontEndFree = false;
    check(is(lan.update(busy), Step::None, "waiting", "front end"), "LAN: a busy front end waits");
    check(is(lan.update(withHost(0)), Step::Search, "searching", "0"), "LAN: a free front end starts the game's search (no login)");
    check(is(lan.update(searchingFor(1)), Step::None, "searching", "0"), "LAN: while nothing is listed it searches");
    const Decision found = lan.update(listing(1, kHostAddress));
    check(is(found, Step::Join, "found") && found.index == kServerId,
          "LAN: the first game, announced from the host's address, is joined by its id");
    Inputs dialog = searchingFor(2);
    dialog.frontEndFree = false;
    dialog.dialogOpen = true;
    check(is(lan.update(dialog), Step::None, "dialog"), "LAN: the load dialog is the player's");
    dialog.nowMs = 90 * kSecond;
    check(is(lan.update(dialog), Step::None, "dialog"), "LAN: for as long as they like, with the search running");
    Inputs closed = searchingFor(91);
    closed.frontEndFree = false;
    check(is(lan.update(closed), Step::StopSearch, "joining"), "LAN: the search is stopped once the dialog is gone");
    check(lan.update(closed).step == Step::None, "LAN: and only once");
    Inputs inSession = withHost(92);
    inSession.sessionActive = true;
    check(is(lan.update(inSession), Step::None, "joined"), "LAN: a session is joined");

    Machine leak(guest_join_rules::Mode::Lan);
    leak.update(withHost(0));
    check(is(leak.update(listing(1, kRealAddress)), Step::GiveUp, "failed", "host_address"),
          "LAN: a game announced from a real address ends the join");
    check(is(leak.update(listing(2, kRealAddress)), Step::None, "failed", "host_address"), "LAN: and the status keeps its reason");
    leak.reset();
    check(is(leak.update(withHost(100)), Step::Search, "searching", "0"), "LAN: a new request starts over, still in LAN mode");

    Machine refused(guest_join_rules::Mode::Lan);
    refused.update(withHost(0));
    refused.update(listing(1, kHostAddress));
    Inputs refusal = searchingFor(2);
    refusal.denyReason = join_deny_rules::kReasonWrongVersion;
    check(is(refused.update(refusal), Step::GiveUp, "failed", "version"), "LAN: a version refusal ends the join too");

    Machine idle(guest_join_rules::Mode::Lan);
    idle.update(withHost(0));
    idle.update(listing(1, kHostAddress));
    idle.update(searchingFor(2));
    check(is(idle.update(searchingFor(12)), Step::None, "searching", "0"), "LAN: 10 idle seconds after the join fail the attempt");
    check(idle.update(listing(14, kHostAddress)).step == Step::None, "LAN: the next pick waits 5 seconds");
    check(idle.update(listing(17, kHostAddress)).step == Step::Join, "LAN: and then joins again");
}

}  // namespace

int main() {
    check(join_status::statusLine({"advertising", ""}) == "state=advertising" &&
              join_status::statusLine({"searching", "3"}) == "state=searching detail=3",
          "a status line is state=<s>, then detail=<d> when there is one");

    Machine machine;
    Inputs busy = ready(0);
    busy.frontEndFree = false;
    check(is(machine.update(busy), Step::None, "waiting", "front end"), "a busy front end waits");

    check(is(machine.update(ready(0)), Step::Search, "searching", "0"), "a free, logged in front end starts the query");
    Inputs querying = ready(1);
    querying.browser = Browser::Querying;
    querying.listed = 4;
    check(is(machine.update(querying), Step::None, "searching", "4"), "while the query runs it reports the listed entries");

    Inputs found = ready(2);
    found.browser = Browser::Querying;
    found.match = 3;
    Decision join = machine.update(found);
    check(is(join, Step::Join, "found") && join.index == 3, "a match joins at its index");

    Inputs dialog = ready(3);
    dialog.frontEndFree = false;
    dialog.dialogOpen = true;
    check(is(machine.update(dialog), Step::None, "dialog"), "the load dialog is left to the player");
    Inputs starting = ready(4);
    starting.frontEndFree = false;
    check(is(machine.update(starting), Step::None, "joining"), "a pending game start is a join in progress");
    Inputs session = ready(5);
    session.frontEndFree = false;
    session.sessionActive = true;
    check(is(machine.update(session), Step::None, "joined"), "a session means joined");
    check(is(machine.update(ready(6)), Step::None, "stopped"), "back at the front end after joining is stopped, not a new search");

    machine.reset();
    check(machine.update(ready(10)).step == Step::Search, "reset starts over");

    // No match: the query ends, the next one waits 5 seconds.
    Machine retry;
    retry.update(ready(0));
    Inputs done = ready(1);
    done.browser = Browser::Done;
    check(is(retry.update(done), Step::None, "searching", "0"), "a finished query without a match ends");
    check(retry.update(ready(5)).step == Step::None, "the next query waits 5 seconds");
    check(retry.update(ready(6)).step == Step::Search, "then the query starts again");

    Machine timeout;
    timeout.update(ready(0));
    Inputs slow = ready(11);
    slow.browser = Browser::Querying;
    timeout.update(slow);
    Inputs late = ready(12);
    late.browser = Browser::Querying;
    timeout.update(late);
    check(timeout.update(ready(16)).step == Step::None && timeout.update(ready(17)).step == Step::Search,
          "a query that finds nothing for 12 seconds ends and is retried after 5");

    // Login: two calls 30 seconds apart, then the player has to log in; no cap on waiting for a host.
    Machine login;
    Inputs offline = ready(0);
    offline.loggedIn = false;
    check(is(login.update(offline), Step::Login, "waiting", "login"), "not logged in: the login is called");
    offline.nowMs = 10 * kSecond;
    check(is(login.update(offline), Step::None, "waiting", "login"), "and not again within 30 seconds");
    offline.nowMs = 30 * kSecond;
    check(login.update(offline).step == Step::Login, "it is called a second time after 30 seconds");
    offline.nowMs = 59 * kSecond;
    check(is(login.update(offline), Step::None, "waiting", "login"), "the second call gets 30 seconds");
    offline.nowMs = 60 * kSecond;
    check(is(login.update(offline), Step::None, "login_needed"), "then the player has to log in, no third call");
    offline.nowMs = 200 * kSecond;
    check(is(login.update(offline), Step::None, "login_needed"), "and it stays that way");
    check(login.update(ready(201)).step == Step::Search, "a login made by the player starts the search");

    // Failed attempts: a join that ends at the idle front end for 10 seconds.
    Machine attempts;
    check(startAndJoin(attempts).step == Step::Join, "attempt 1 joins");
    check(is(attempts.update(ready(2)), Step::None, "joining"), "an idle front end right after the join is not a failure yet");
    check(is(attempts.update(ready(11)), Step::None, "joining"), "nor after 9 seconds");
    check(is(attempts.update(ready(12)), Step::None, "searching", "0"), "10 idle seconds fail the attempt and search again");
    check(attempts.update(ready(16)).step == Step::None && attempts.update(ready(17)).step == Step::Search,
          "the next query starts 5 seconds later");
    Inputs again = ready(18);
    again.match = 0;
    again.browser = Browser::Done;
    check(attempts.update(again).step == Step::Join, "attempt 2 joins");
    check(attempts.update(ready(19)).step == Step::None, "idle");
    check(attempts.update(ready(29)).step == Step::None, "attempt 2 fails");
    check(attempts.update(ready(34)).step == Step::Search, "query again");
    Inputs third = ready(35);
    third.match = 1;
    third.browser = Browser::Done;
    check(attempts.update(third).step == Step::Join, "attempt 3 joins");
    check(attempts.update(ready(36)).step == Step::None, "idle");
    check(is(attempts.update(ready(46)), Step::GiveUp, "failed", "no join"), "the third failure gives up and stops the query");
    check(is(attempts.update(ready(100)), Step::None, "failed", "no join"), "nothing more until the request changes");
    attempts.reset();
    check(attempts.update(ready(101)).step == Step::Search, "a new request tries again");

    // A dialog that stays open is not idle, however long it takes.
    Machine patient;
    startAndJoin(patient);
    Inputs open = ready(2);
    open.frontEndFree = false;
    open.dialogOpen = true;
    patient.update(open);
    open.nowMs = 200 * kSecond;
    check(is(patient.update(open), Step::None, "dialog"), "the player may take as long as they like");
    Inputs closed = ready(201);
    check(patient.update(closed).step == Step::None, "a cancelled dialog starts the idle count");
    check(is(patient.update(ready(211)), Step::None, "searching", "0"), "which counts as a failed attempt");

    // A refusal no retry changes ends the join at once, with its reason in the status.
    Machine refused;
    startAndJoin(refused);
    Inputs denied = ready(2);
    denied.denyReason = join_deny_rules::kReasonWrongVersion;
    check(is(refused.update(denied), Step::GiveUp, "failed", "version"), "a version refusal gives up at once");
    check(is(refused.update(ready(3)), Step::None, "failed", "version"), "and the status keeps its reason");
    Machine badPassword;
    startAndJoin(badPassword);
    denied.denyReason = join_deny_rules::kReasonBadPassword;
    check(is(badPassword.update(denied), Step::GiveUp, "failed", "password"), "a password refusal gives up at once");
    Machine otherReason;
    startAndJoin(otherReason);
    denied.denyReason = 3;
    check(is(otherReason.update(denied), Step::None, "joining"), "any other refusal is left to the idle count");

    lanPath();
    return g_failures == 0 ? 0 : 1;
}
