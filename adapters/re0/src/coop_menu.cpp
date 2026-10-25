#include "coop_menu.h"

#include <string>
#include <vector>

#include "character_owner.h"
#include "coop_menu_rule.h"
#include "debug_stats.h"
#include "lobby.h"
#include "native_menu.h"
#include "option_values.h"
#include "save_blocks.h"
#include "save_index_rule.h"
#include "save_load_rule.h"
#include "session_slot.h"
#include "state_mode.h"
#include "title_load.h"

namespace {

using character_owner::Character;
using coop_menu_rule::Player;
using lobby_wire::Value;
using native_menu_rule::Event;
using native_menu_rule::EventKind;
using native_menu_rule::Reply;
using native_menu_rule::RowKind;

constexpr native_menu_rule::MenuDef kMenu{coop_menu_rule::kMenuId,
                                          "coop.menu",
                                          {{{"host_player", RowKind::Info, ""},
                                            {"guest_player", RowKind::Info, ""},
                                            {"character", RowKind::Choice, "coop.character"},
                                            {"outfit", RowKind::Choice, "coop.outfit"},
                                            {"world_pause", RowKind::Choice, "coop.world_pause"},
                                            {"partner_mode", RowKind::Info, ""},
                                            {"save", RowKind::Choice, "coop.save"},
                                            {"start", RowKind::Action, "coop.start"}}},
                                          native_menu_rule::kMaxRows};

constexpr int kNoPick = save_load_rule::kUnknownSlot;

struct Context {
    debug_stats::Snapshot session;
    lobby::View lobby;
    bool isHost;
};

Context context() {
    Context c{debug_stats::snapshot(), lobby::view(), false};
    c.isHost = !c.session.linked || character_owner::isHost();
    return c;
}

// The host's character from the launcher's host options; this machine's own choice while no launcher said one.
Character hostCharacter(const Context& c) {
    if (const Value* option = c.lobby.state.option(option_values::kCharacterKey)) {
        if (const auto character = option_values::characterOf(option->value)) return *character;
    }
    return c.isHost ? character_owner::plannedLocalCharacter() : Character::Rebecca;
}

Character localCharacter(const Context& c) {
    const Character host = hostCharacter(c);
    return c.isHost ? host : character_owner::other(host);
}

const lobby_wire::Presence* presenceOf(const Context& c, uint8_t slot, bool local) {
    if (local) return &c.lobby.own;
    for (const lobby::PeerPresence& peer : c.lobby.peers) {
        if (peer.slot == slot) return &peer.presence;
    }
    return nullptr;
}

Player playerOf(const Context& c, const std::string& name, bool host, uint8_t slot, bool local) {
    Player player{name, host, local, std::nullopt};
    if (const lobby_wire::Presence* presence = presenceOf(c, slot, local)) player.presence = *presence;
    return player;
}

// The players host first: the launcher's members, or without its text the session's peers.
void fillPlayers(const Context& c, coop_menu_rule::Inputs& in) {
    for (const lobby_wire::Member& member : c.lobby.state.members) {
        std::optional<Player>& row = member.host ? in.host : in.guest;
        if (!row) row = playerOf(c, member.name, member.host, member.slot, member.local);
    }
    if (c.lobby.haveState) return;
    if (!in.host) in.host = playerOf(c, c.isHost ? "You" : "Host", true, c.session.hostSlot, c.isHost);
    for (const PeerInfo& peer : c.session.peers) {
        if (peer.slot != c.session.hostSlot && !in.guest) in.guest = playerOf(c, peer.name, false, peer.slot, false);
    }
    if (!c.isHost) in.guest = playerOf(c, "You", false, c.session.localSlot, true);
}

std::string labelOf(const std::vector<Value>& values, const std::string& value) {
    for (const Value& entry : values) {
        if (entry.value == value) return entry.label;
    }
    return value;
}

std::string optionLabel(const Context& c, std::string_view key) {
    const Value* option = c.lobby.state.option(key);
    return option ? option->label : coop_menu_rule::kNone;
}

std::string ownOutfit(const Context& c, Character character) {
    const Value* own = c.lobby.state.ownValue(option_values::outfitKey(character));
    return own ? own->value : std::string();
}

// The slot the session will play as this machine sees it: the host's own pick, a guest the host's announced pick.
int pickedSlot(const Context& c) {
    if (c.isHost) return session_slot::order().slot;
    const lobby_wire::Presence* host = presenceOf(c, c.session.hostSlot, false);
    return host && (host->flags & lobby_wire::kHasPick) ? host->pickSlot : kNoPick;
}

std::string slotLabel(int slot) {
    if (!save_load_rule::isPlayerSlot(slot)) return coop_menu_rule::kNone;
    const auto summary = save_blocks::summary(slot);
    return summary ? save_index_rule::label(slot, *summary) : coop_menu_rule::kNone;
}

native_menu_rule::PageView view() {
    const Context c = context();
    coop_menu_rule::Inputs in;
    in.isHost = c.isHost;
    fillPlayers(c, in);
    const Character local = localCharacter(c);
    in.character = labelOf(c.lobby.state.valuesOf(option_values::kCharacterKey), std::string(option_values::valueOf(local)));
    if (in.character == option_values::valueOf(local)) in.character = character_owner::name(local);
    const std::vector<Value> outfits = c.lobby.state.outfitsOf(option_values::valueOf(local));
    in.outfit = outfits.empty() ? coop_menu_rule::kNone : labelOf(outfits, ownOutfit(c, local));
    in.outfitChoosable = outfits.size() > 1;
    in.worldPause = optionLabel(c, option_values::kWorldPauseKey);
    in.partnerMode = c.lobby.state.option(option_values::kPartnerModeKey) ? optionLabel(c, option_values::kPartnerModeKey)
                                                                           : partnerModeName(state_mode::on() ? PartnerMode::State : PartnerMode::Input);
    const int slot = pickedSlot(c);
    in.save = slotLabel(slot);
    in.savePicked = in.save != coop_menu_rule::kNone;
    in.savesOffered = save_blocks::request() != 0;
    in.start = title_load::status();
    return coop_menu_rule::view(in);
}

// The value after the current one in a list the launcher offers, sent back to the launcher as the player's choice; the
// launcher's next LOBBY_STATE brings it to every screen and lobby::apply makes it act.
Reply stepOption(std::string_view key, const std::vector<Value>& values, const std::string& current, int step) {
    int index = -1;
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i].value == current) index = static_cast<int>(i);
    }
    const int next = coop_menu_rule::stepIndex(index, static_cast<int>(values.size()), step);
    if (next < 0 || values.size() < 2) return Reply::Reject;
    return lobby::sendOption(key, values[static_cast<size_t>(next)].value) ? Reply::Stay : Reply::Reject;
}

Reply onMenu(const Event&) { return Reply::Stay; }

Reply onCharacter(const Event& event) {
    const Context c = context();
    if (!c.isHost) return Reply::Reject;
    return stepOption(option_values::kCharacterKey, c.lobby.state.valuesOf(option_values::kCharacterKey),
                      std::string(option_values::valueOf(hostCharacter(c))), event.step);
}

Reply onOutfit(const Event& event) {
    const Context c = context();
    const Character local = localCharacter(c);
    return stepOption(option_values::outfitKey(local), c.lobby.state.outfitsOf(option_values::valueOf(local)), ownOutfit(c, local),
                      event.step);
}

Reply onWorldPause(const Event& event) {
    const Context c = context();
    if (!c.isHost) return Reply::Reject;
    const Value* current = c.lobby.state.option(option_values::kWorldPauseKey);
    return stepOption(option_values::kWorldPauseKey, c.lobby.state.valuesOf(option_values::kWorldPauseKey),
                      current ? current->value : std::string(), event.step);
}

// The slots the host may pick: none (the game's own load list decides), then every used slot.
std::vector<int> pickableSlots() {
    std::vector<int> slots{kNoPick};
    for (int slot = 0; slot < save_load_rule::kPlayerSlots; ++slot) {
        const auto summary = save_blocks::summary(slot);
        if (summary && summary->used) slots.push_back(slot);
    }
    return slots;
}

// The host's pick is the session's order (session_slot), which the launcher's hosting screen gets too.
Reply onSave(const Event& event) {
    if (!context().isHost) return Reply::Reject;
    const std::vector<int> slots = pickableSlots();
    int index = -1;
    const int current = session_slot::order().slot;
    for (size_t i = 0; i < slots.size(); ++i) {
        if (slots[i] == current) index = static_cast<int>(i);
    }
    const int slot = slots[static_cast<size_t>(coop_menu_rule::stepIndex(index, static_cast<int>(slots.size()), event.step))];
    if (slot == kNoPick) {
        session_slot::setOrder({});
        lobby::sendOption(option_values::kLoadSlotKey, "");
        return Reply::Stay;
    }
    session_slot::setOrder({slot, save_blocks::hash(slot)});
    lobby::sendOption(option_values::kLoadSlotKey, std::to_string(slot));
    return Reply::Stay;
}

Reply onStart(const Event&) {
    const save_load_rule::Order order = session_slot::order();
    if (!context().isHost || !order.active()) return Reply::Reject;
    const int32_t slot = order.slot;
    native_menu::afterClose([slot] { title_load::startHost(slot); });
    return Reply::Close;
}

}  // namespace

namespace coop_menu {

void enable() {
    native_menu::registerMenu(kMenu, view);
    native_menu::registerAction("coop.menu", onMenu);
    native_menu::registerAction("coop.character", onCharacter);
    native_menu::registerAction("coop.outfit", onOutfit);
    native_menu::registerAction("coop.world_pause", onWorldPause);
    native_menu::registerAction("coop.save", onSave);
    native_menu::registerAction("coop.start", onStart);
}

}  // namespace coop_menu
