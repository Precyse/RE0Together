"""The checks of the echo rig as data (docs/SR2_NOTES.md "Echo testing"): each row names the adapter's logger::write format
string verbatim next to the regex that finds its line. echo_checks_test.py fails when a format string is no longer in
adapters/sr2/src or a regex stops matching a line made from its format, so a drifted adapter fails a test, not a live run."""
import re

from echo_checks import (Check, EACH_MEMBER, EACH_REMOTE, Expect, LAST, NONE, ONCE, SOURCE_STATUS, WHO_ALL, WHO_GUESTS,
                         WHO_HOST, WHO_OTHERS, WHO_TARGET, remote_slots)

LINK_WINDOW_S = 10.0
GAME_SOCKET_WINDOW_S = 120.0
PLAYER_WINDOW_S = 120.0
HEX = r"0x[0-9A-F]+"
HOST_SLOT = 0
FIRST_GUEST_SLOT = 1
HEX_BASE = 16
CLEAN_TUNNEL_COUNTERS = ("no_peer", "not_virtual", "wrong_dst", "inbox_full", "oversize")
HUB_ZERO = ("not_linked", "no_slot")
REMOTE_HUMAN_FORMAT = ("remote human 0x%X owner slot %u member %s flags 0x%02X downed %d hidden 0x%02X render 0x%X skin %u "
                       "type 0x%X record 0x%X%s")
TUNNEL_COUNTERS_FORMAT = ("sent %llu received %llu dropped no_peer %llu not_virtual %llu wrong_dst %llu inbox_full %llu "
                          "oversize %llu, large %llu")
REFUSAL_REASON_FORMAT = ("version %u players %u flags %u content 0x%016llX, ours version %u players %u flags %u "
                         "content 0x%016llX")


def port_is_hubs(ctx, _instance, match, _lines, _slot):
    port = int(match["port"])
    return None if port == ctx.port else f"launcher port {port}, the hub is on {ctx.port}"


def address_is_hubs(ctx, instance, match, _lines, _slot):
    return None if match["address"] == ctx.address(instance) else f"address {match['address']}, the hub expects {ctx.address(instance)}"


def count_is_linked(group):
    def verify(ctx, _instance, match, _lines, _slot):
        wanted = len(ctx.linked())
        return None if int(match[group]) == wanted else f"{match[group]} members, {wanted} instances are linked"
    return verify


def counters_are_zero(_ctx, _instance, match, _lines, _slot):
    dirty = [name for name in CLEAN_TUNNEL_COUNTERS if int(match[name]) != 0]
    return f"{', '.join(dirty)} not 0" if dirty else None


def partner_owner(owner_slot):
    """The default partner must be the human whose owner slot is owner_slot (a remote human line names it)."""
    def verify(_ctx, _instance, match, lines, _slot):
        human = re.compile(rf"remote human {match['human']} owner slot (\d+)")
        owners = {int(found[1]) for line in lines if (found := human.search(line))}
        return None if owner_slot in owners else f"human {match['human']} is owned by {sorted(owners) or 'nobody'}, expected slot {owner_slot}"
    return verify


def mask_bit(wanted):
    """The pause byte's bit for the target slot after the change must be set (or clear)."""
    def verify(_ctx, _instance, match, _lines, slot):
        bit = (int(match["after"], HEX_BASE) >> slot) & 1
        return None if bit == wanted else f"mask 0x{match['after']} has bit {slot} = {bit}"
    return verify


def picker_lists_remotes(ctx, instance, match, _lines, _slot):
    listed = {int(n) for n in re.findall(r"\d+", match["picker"])}
    wanted = remote_slots(ctx, instance)
    return None if listed == wanted else f"picker slots {sorted(listed)}, the remote members are {sorted(wanted)}"


def hud_lists_remotes(ctx, instance, match, _lines, _slot):
    listed = {int(n) for n in re.findall(r"slot (\d+)", match["humans"])}
    wanted = remote_slots(ctx, instance)
    if int(match["n"]) != len(wanted) or listed != wanted:
        return f"{match['n']} humans, slots {sorted(listed)}, the remote members are {sorted(wanted)}"
    return None


def players_differ(_ctx, _instance, match, _lines, _slot):
    return "refusal does not name different player counts" if match["theirs"] == match["ours"] else None


def not_joined(_ctx, _instance, match, _lines, _slot):
    return "still joined" if match["state"] == "joined" else None


def kicked_slot(_ctx, _instance, match, _lines, slot):
    return None if int(match["slot"]) == slot else f"kicked slot {match['slot']}, expected {slot}"


LINK = Check("link", (
    Expect("steam transport", WHO_ALL, r"tunnel: steam transport, launcher port (?P<port>\d+)", ONCE, within_s=LINK_WINDOW_S,
           format="tunnel: steam transport, launcher port %u", verify=port_is_hubs),
    Expect("own address", WHO_ALL, r"tunnel: own address (?P<address>[\d.]+)", LAST, within_s=LINK_WINDOW_S,
           format="tunnel: own address %s", samples=("10.1.2.3",), verify=address_is_hubs),
    Expect("game socket bound", WHO_ALL, r"game socket: connection reset reports off", ONCE, within_s=GAME_SOCKET_WINDOW_S,
           format="game socket: connection reset reports off"),
))

JOINED = Check("joined", (
    Expect("host status in_session", WHO_HOST, r"state=in_session detail=(?P<n>\d+)", LAST, SOURCE_STATUS, PLAYER_WINDOW_S,
           fragments=('"in_session"',), sample="state=in_session detail=3", verify=count_is_linked("n")),
    Expect("session members", WHO_HOST, r"trace: session members (?P<n>\d+)", LAST,
           format="trace: session members %zu", verify=count_is_linked("n")),
    Expect("member per slot", WHO_HOST, rf"trace: member {HEX} slot (?P<slot>\d+) state \d+ (?:local|remote)", EACH_MEMBER,
           format="member 0x%X slot %u state %u %s", prefix="trace: ", samples=(None, None, None, "remote")),
    Expect("slot inside the session", WHO_HOST, r"is outside the session slots", NONE,
           format="trace: remote %s is outside the session slots 0..%u"),
    Expect("every human has a member", WHO_HOST, r"matches no member", NONE,
           format="partner: human 0x%X owner slot %u matches no member"),
    Expect("guest status joined", WHO_GUESTS, r"state=joined", LAST, SOURCE_STATUS, PLAYER_WINDOW_S,
           fragments=('"joined"',), sample="state=joined"),
))

PROBE_A = Check("probe-a", (
    Expect("remote human per remote", WHO_ALL, rf"trace: remote human {HEX} owner slot (?P<slot>\d+) member {HEX} ", EACH_REMOTE,
           format=REMOTE_HUMAN_FORMAT, prefix="trace: ", samples=(None, None, "0x1A")),
    Expect("guest partner is the host", WHO_GUESTS, rf"trace: default partner now human (?P<human>{HEX}) member {HEX}", LAST,
           format="trace: default partner now human 0x%X member 0x%X%s", samples=(None, None, ""),
           verify=partner_owner(HOST_SLOT)),
    Expect("host partner is the first guest", WHO_HOST, rf"trace: default partner now human (?P<human>{HEX}) member {HEX}", LAST,
           format="trace: default partner now human 0x%X member 0x%X%s", samples=(None, None, ""),
           verify=partner_owner(FIRST_GUEST_SLOT)),
))

TUNNEL_CLEAN = Check("tunnel-clean", (
    Expect("tunnel counters", WHO_ALL,
           r"tunnel: sent (?P<sent>\d+) received (?P<received>\d+) dropped no_peer (?P<no_peer>\d+) not_virtual (?P<not_virtual>\d+) "
           r"wrong_dst (?P<wrong_dst>\d+) inbox_full (?P<inbox_full>\d+) oversize (?P<oversize>\d+), large (?P<large>\d+)", LAST,
           format=TUNNEL_COUNTERS_FORMAT, prefix="tunnel: ", verify=counters_are_zero),
), hub_zero=HUB_ZERO)

PAUSE = Check("pause", (
    Expect("slot paused", WHO_HOST, r"trace: pause: slot @SLOT@ sent 1, mask 0x[0-9A-F]+ -> 0x(?P<after>[0-9A-F]+), local state \d+",
           ONCE, within_s=PLAYER_WINDOW_S, format="trace: pause: slot %u sent %u, mask 0x%02X -> 0x%02X, local state %u, %s",
           samples=(None, 1, None, "02", None, "apply"), verify=mask_bit(1)),
    Expect("slot resumed", WHO_HOST, r"trace: pause: slot @SLOT@ sent 0, mask 0x[0-9A-F]+ -> 0x(?P<after>[0-9A-F]+), local state \d+",
           ONCE, within_s=PLAYER_WINDOW_S, format="trace: pause: slot %u sent %u, mask 0x%02X -> 0x%02X, local state %u, %s",
           samples=(None, 0, "02", None, None, "apply"), after="slot paused", verify=mask_bit(0)),
    Expect("world resumed", WHO_HOST, r"trace: pause: apply 0x8CA190 with mask 0x[0-9A-F]+: count 1 -> 0, world paused 1 -> 0", ONCE,
           format="trace: pause: apply 0x8CA190 with mask 0x%02X: count %u -> %u, world paused %u -> %u, remote pause %u -> %u",
           samples=(None, 1, 0, 1, 0), after="slot resumed"),
), needs_target=True)

KICK = Check("kick", (
    Expect("picker lists every remote", WHO_HOST, r"trace: kick: picker for (?P<n>\d+) remote members: slots (?P<picker>[\d ]+)",
           ONCE, within_s=PLAYER_WINDOW_S, format="trace: kick: picker for %zu remote members: slots %s", samples=(None, "1 2"),
           verify=picker_lists_remotes),
    Expect("picked slot kicked", WHO_HOST, rf"trace: kick: kicking slot (?P<slot>\d+) member {HEX} \(chosen\)", ONCE,
           within_s=PLAYER_WINDOW_S, format="trace: kick: %s slot %u member 0x%08X (%s)", samples=("kicking", None, None, "chosen"),
           after="picker lists every remote", verify=kicked_slot),
    Expect("kicked guest left the session", WHO_TARGET, r"state=(?P<state>\w+)", LAST, SOURCE_STATUS, PLAYER_WINDOW_S,
           sample="state=stopped", verify=not_joined),
    Expect("other guests dropped the leaver", WHO_OTHERS, r"trace: leave: slot @SLOT@, dropped \d+ locally", ONCE, within_s=LINK_WINDOW_S,
           format="trace: leave: slot %u, dropped %u locally, renamed %zu, reissued %zu"),
), needs_target=True)

LEAVE = Check("leave", (
    Expect("host saw the pause slot go", WHO_HOST, r"trace: pause: slot @SLOT@ left, mask 0x[0-9A-F]+ -> 0x[0-9A-F]+", ONCE,
           within_s=LINK_WINDOW_S, format="trace: pause: slot %u left, mask 0x%02X -> 0x%02X, %s"),
    Expect("host took the leaver over", WHO_HOST, r"trace: leave: slot @SLOT@, took over \d+, renamed \d+, reissued \d+ to \d+ member",
           ONCE, within_s=LINK_WINDOW_S,
           format="trace: leave: slot %u, took over %zu, renamed %zu, reissued %zu to %zu member(s)"),
    Expect("guests dropped the leaver", WHO_OTHERS, r"trace: leave: slot @SLOT@, dropped \d+ locally, renamed \d+, reissued \d+", ONCE,
           within_s=LINK_WINDOW_S, format="trace: leave: slot %u, dropped %u locally, renamed %zu, reissued %zu"),
), includes=("tunnel-clean",), needs_target=True)

HUD = Check("hud", (
    Expect("minimap arrows", WHO_HOST, r"trace: hud: minimap arrows for (?P<n>\d+) remote humans (?P<humans>.*)", LAST,
           format="trace: hud: %s arrows for %u remote humans %s", samples=("minimap", None, "0x00001234 (slot 1)"),
           verify=hud_lists_remotes),
    Expect("pause map arrows", WHO_HOST, r"trace: hud: pause map arrows for (?P<n>\d+) remote humans (?P<humans>.*)", LAST,
           within_s=PLAYER_WINDOW_S, format="trace: hud: %s arrows for %u remote humans %s",
           samples=("pause map", None, "0x00001234 (slot 1)"),
           verify=hud_lists_remotes),
))

WAYPOINT = Check("waypoint", (
    Expect("waypoint per remote", WHO_HOST, r"trace: waypoint: to #PLAYER\d+# only \(slot (?P<slot>\d+), member " + HEX, EACH_REMOTE,
           format="trace: waypoint: to %s only (slot %u, member 0x%X), %u bytes", samples=("#PLAYER2#",)),
    Expect("every waypoint names a remote", WHO_HOST, r"names no remote member", NONE,
           format="trace: waypoint: %s names no remote member, sent to every remote", samples=("#PLAYER2#",)),
))

REFUSAL = Check("refusal", (
    Expect("host refuses the guest", WHO_HOST,
           r"version gate: join_req from [\d.]+:\d+ refused, version \d+ players (?P<theirs>\d+) .*ours version \d+ players (?P<ours>\d+)",
           ONCE, within_s=PLAYER_WINDOW_S, format="version gate: %s %u.%u.%u.%u:%u refused, %s",
           fragments=(REFUSAL_REASON_FORMAT, "join_req from"),
           samples=("join_req from", None, None, None, None, None, "version 2 players 3 flags 1 content 0x1F, ours version 2 players 4 flags 1 content 0x1F"),
           verify=players_differ),
    Expect("guest status failed", WHO_TARGET, r"state=failed", LAST, SOURCE_STATUS, PLAYER_WINDOW_S,
           fragments=('"failed"',), sample="state=failed detail=refused"),
), needs_target=True)

ADMISSION = Check("admission", (
    Expect("second guest waits", WHO_HOST, rf"trace: admission: member {HEX} waits, member {HEX} is being admitted, request \d+", ONCE,
           within_s=PLAYER_WINDOW_S, format="trace: admission: member 0x%X waits, member 0x%X is being admitted, request %u"),
    Expect("second guest admitted", WHO_HOST, rf"trace: admission: member {HEX} is admitted now", ONCE, within_s=PLAYER_WINDOW_S,
           format="trace: admission: member 0x%X is admitted now", after="second guest waits"),
))

CHECKS = {check.name: check for check in
          (LINK, JOINED, PROBE_A, TUNNEL_CLEAN, PAUSE, KICK, LEAVE, HUD, WAYPOINT, REFUSAL, ADMISSION)}
