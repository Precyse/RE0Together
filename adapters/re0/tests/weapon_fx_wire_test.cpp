// Checks the WEAPON_FX wire format (no game): entry sizes, round trips of every entry kind, and the refusals (unknown op,
// a count past the payload, trailing bytes, a site that does not belong to its op, values the engine would assert on,
// non-finite numbers). Exit 0 when every check passes.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "../src/weapon_fx_wire.h"

namespace {

namespace wire = weapon_fx_wire;
namespace sites = weapon_fx_sites;
using wire::Entry;
using wire::Op;

constexpr uint32_t kSenderMs = 123456;
constexpr uint32_t kSeq = 7;
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
int g_failures = 0;

void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

uint8_t siteOf(sites::Primitive primitive) {
    for (size_t id = 0; id < sites::kSites.size(); ++id) {
        if (sites::kSites[id].primitive == primitive) return static_cast<uint8_t>(id);
    }
    return sites::kNoSite;
}

Entry sound() {
    Entry entry;
    entry.op = Op::Se;
    entry.site = siteOf(sites::Primitive::PlaySe);
    entry.number = 0x1f00;
    entry.level = 1.0f;
    return entry;
}

Entry flash() {
    Entry entry;
    entry.op = Op::Flash;
    entry.site = siteOf(sites::Primitive::MuzzleFlash);
    entry.kind = 0;
    entry.level = 1.0f;
    return entry;
}

Entry casing() {
    Entry entry;
    entry.op = Op::Shoot;
    entry.site = siteOf(sites::Primitive::Shoot);
    entry.kind = 1;
    entry.muzzle = 1;
    return entry;
}

Entry projectile() {
    Entry entry;
    entry.op = Op::Shoot;
    entry.site = siteOf(sites::Primitive::Shoot);
    entry.kind = wire::kProjectileKind;
    entry.pos[0] = 1.5f;
    entry.pos[1] = -2.0f;
    entry.pos[2] = 30.0f;
    entry.quat[1] = std::sqrt(0.5f);
    entry.quat[3] = std::sqrt(0.5f);
    entry.autoPitch = 0.25f;
    entry.manualPitch = -0.5f;
    entry.weapon = 7;
    return entry;
}

Entry effect() {
    Entry entry;
    entry.op = Op::Effect;
    entry.site = siteOf(sites::Primitive::Effect);
    entry.number = 0xb;
    entry.owner = wire::kOwnerWeaponUnit;
    entry.attach = wire::kAttachWeaponUnit;
    entry.joint = wire::kNoJoint;
    entry.pos[0] = 4.0f;
    entry.rot[2] = 0.5f;
    return entry;
}

Entry ripple() {
    Entry entry;
    entry.op = Op::Ripple;
    entry.site = siteOf(sites::Primitive::Ripple);
    entry.kind = 1;
    entry.pos[1] = 9.0f;
    return entry;
}

Entry stow() {
    Entry entry;
    entry.op = Op::Stow;
    entry.site = siteOf(sites::Primitive::Stow);
    return entry;
}

Entry wick() {
    Entry entry;
    entry.op = Op::Wick;
    entry.site = siteOf(sites::Primitive::Wick);
    return entry;
}

wire::Batch batchOf(const Entry& entry) {
    wire::Batch batch;
    batch.senderMs = kSenderMs;
    batch.seq = kSeq;
    batch.character = 1;
    batch.entries.push_back(entry);
    return batch;
}

std::vector<uint8_t> bytesOf(const Entry& entry) { return *wire::encode(batchOf(entry)); }

bool decodes(const std::vector<uint8_t>& bytes) { return wire::decode(bytes).has_value(); }

void testSizes() {
    check(sizeof(wire::Header) == 12 && sizeof(wire::SeWire) == 8 && sizeof(wire::FlashWire) == 8, "header 12, sound 8, flash 8");
    check(sizeof(wire::ShootWire) == 4 && sizeof(wire::ProjectileWire) == 44, "casing 4, projectile 44");
    check(sizeof(wire::EffectWire) == 32 && sizeof(wire::RippleWire) == 16 && sizeof(wire::ThrowableWire) == 4,
          "effect 32, ripple 16, stow and wick 4");
    check(bytesOf(sound()).size() == 12 + 8, "an encoded sound is header plus 8");
    check(bytesOf(casing()).size() == 12 + 4, "an encoded casing is header plus 4");
    check(bytesOf(projectile()).size() == 12 + 44, "an encoded projectile is header plus 44");
    check(bytesOf(effect()).size() == 12 + 32, "an encoded effect is header plus 32");
    check(bytesOf(wick()).size() == 12 + 4, "an encoded wick is header plus 4");
}

void testRoundTrip() {
    wire::Batch batch;
    batch.senderMs = kSenderMs;
    batch.seq = kSeq;
    batch.character = 1;
    batch.entries = {sound(), flash(), casing(), projectile(), effect(), ripple(), stow(), wick()};
    const auto bytes = wire::encode(batch);
    check(bytes.has_value(), "a batch of every kind encodes");
    const auto back = bytes ? wire::decode(*bytes) : std::nullopt;
    check(back.has_value() && back->senderMs == kSenderMs && back->seq == kSeq && back->character == 1 && back->entries.size() == 8,
          "the header and the count come back");
    if (!back) return;
    const Entry& s = back->entries[0];
    check(s.op == Op::Se && s.site == sound().site && s.number == 0x1f00 && s.level == 1.0f, "a sound comes back");
    check(back->entries[1].op == Op::Flash && back->entries[1].kind == 0 && back->entries[1].level == 1.0f, "a flash comes back");
    check(back->entries[2].op == Op::Shoot && back->entries[2].kind == 1 && back->entries[2].muzzle == 1, "a casing comes back");
    const Entry& p = back->entries[3];
    check(wire::isProjectile(p) && p.pos[0] == 1.5f && p.pos[2] == 30.0f && p.quat[3] == projectile().quat[3] &&
              p.autoPitch == 0.25f && p.manualPitch == -0.5f && p.weapon == 7,
          "a projectile comes back with its matrix, pitches and weapon");
    const Entry& e = back->entries[4];
    check(e.op == Op::Effect && e.number == 0xb && e.owner == wire::kOwnerWeaponUnit && e.attach == wire::kAttachWeaponUnit &&
              e.joint == wire::kNoJoint && e.pos[0] == 4.0f && e.rot[2] == 0.5f,
          "an effect comes back");
    check(back->entries[5].op == Op::Ripple && back->entries[5].kind == 1 && back->entries[5].pos[1] == 9.0f, "a ripple comes back");
    check(back->entries[6].op == Op::Stow && back->entries[7].op == Op::Wick, "stow and wick come back");
}

void testEncodeLimits() {
    wire::Batch empty;
    check(!wire::encode(empty).has_value(), "an empty batch is not encoded");
    wire::Batch full = batchOf(sound());
    full.entries.assign(wire::kMaxEntries, sound());
    check(wire::encode(full).has_value(), "a full batch is encoded");
    full.entries.push_back(sound());
    check(!wire::encode(full).has_value(), "a batch over the limit is not encoded");
}

void testHeaderRefusals() {
    auto bytes = bytesOf(sound());
    check(decodes(bytes), "a valid message decodes");
    check(!wire::decode(std::span<const uint8_t>(bytes.data(), sizeof(wire::Header) - 1)).has_value(), "a short header is refused");
    auto zero = bytes;
    zero[offsetof(wire::Header, count)] = 0;
    check(!decodes(zero), "a count of zero is refused");
    auto many = bytes;
    many[offsetof(wire::Header, count)] = wire::kMaxEntries + 1;
    check(!decodes(many), "a count over the limit is refused");
    auto past = bytes;
    past[offsetof(wire::Header, count)] = 2;
    check(!decodes(past), "a count past the payload is refused");
    auto trailing = bytes;
    trailing.push_back(0);
    check(!decodes(trailing), "trailing bytes are refused");
    auto character = bytes;
    character[offsetof(wire::Header, character)] = wire::kMaxCharacter + 1;
    check(!decodes(character), "an unknown character is refused");
}

void testEntryRefusals() {
    const size_t at = sizeof(wire::Header);
    auto unknown = bytesOf(sound());
    unknown[at] = 0;
    check(!decodes(unknown), "op 0 is refused");
    unknown[at] = static_cast<uint8_t>(Op::Wick) + 1;
    check(!decodes(unknown), "an unknown op is refused");

    auto wrongSite = bytesOf(sound());
    wrongSite[at + offsetof(wire::SeWire, site)] = siteOf(sites::Primitive::MuzzleFlash);
    check(!decodes(wrongSite), "a site of another op is refused");
    wrongSite[at + offsetof(wire::SeWire, site)] = static_cast<uint8_t>(sites::kSites.size());
    check(!decodes(wrongSite), "a site past the table is refused");

    Entry loud = sound();
    loud.number = 0x1dff;
    check(!decodes(bytesOf(loud)), "a sound below the engine's range is refused");
    loud.number = 0x290d;
    check(!decodes(bytesOf(loud)), "a sound above the engine's range is refused");
    loud.number = 0x290c;
    check(decodes(bytesOf(loud)), "the last sound of the engine's range is accepted");
    loud.level = kNaN;
    check(!decodes(bytesOf(loud)), "a NaN volume is refused");
    loud.level = wire::kMaxLevel + 1.0f;
    check(!decodes(bytesOf(loud)), "a volume over the limit is refused");
    loud.level = -0.5f;
    check(!decodes(bytesOf(loud)), "a negative volume is refused");

    Entry wesker = sound();
    for (size_t id = 0; id < sites::kSites.size(); ++id) {
        if (sites::kSites[id].primitive == sites::Primitive::GenericSe) wesker.site = static_cast<uint8_t>(id);
    }
    wesker.number = 0xa54;
    check(decodes(bytesOf(wesker)), "Wesker's sound is accepted at its own site");
    wesker.number = 0x1f00;
    check(!decodes(bytesOf(wesker)), "a weapon sound at Wesker's site is refused");

    Entry bad = casing();
    bad.kind = wire::kMaxShootKind + 1;
    check(!decodes(bytesOf(bad)), "an unknown shoot kind is refused");
    bad = casing();
    bad.muzzle = wire::kMaxMuzzle + 1;
    check(!decodes(bytesOf(bad)), "an unknown muzzle is refused");

    Entry thrown = projectile();
    thrown.pos[1] = std::numeric_limits<float>::infinity();
    check(!decodes(bytesOf(thrown)), "an infinite projectile position is refused");
    thrown = projectile();
    thrown.quat[3] = 0.0f;
    thrown.quat[1] = 0.0f;
    check(!decodes(bytesOf(thrown)), "a projectile rotation that is not a unit quaternion is refused");
    thrown = projectile();
    thrown.manualPitch = kNaN;
    check(!decodes(bytesOf(thrown)), "a NaN pitch is refused");

    Entry spark = effect();
    spark.owner = wire::kOwnerWeaponUnit + 1;
    check(!decodes(bytesOf(spark)), "an unknown effect owner is refused");
    spark = effect();
    spark.attach = wire::kAttachWeaponUnit + 1;
    check(!decodes(bytesOf(spark)), "an unknown effect attach is refused");
    spark = effect();
    spark.joint = wire::kNoJoint - 1;
    check(!decodes(bytesOf(spark)), "a joint below -1 is refused");
    spark = effect();
    spark.rot[0] = kNaN;
    check(!decodes(bytesOf(spark)), "a NaN effect rotation is refused");

    Entry splash = ripple();
    splash.kind = wire::kMaxRippleSub + 1;
    check(!decodes(bytesOf(splash)), "an unknown ripple sound is refused");
    splash = ripple();
    splash.pos[2] = kNaN;
    check(!decodes(bytesOf(splash)), "a NaN ripple position is refused");
}

void testAcceptable() {
    check(wire::acceptable(sound()) && wire::acceptable(projectile()) && wire::acceptable(effect()), "well formed entries are acceptable");
    Entry loud = sound();
    loud.number = 0;
    check(!wire::acceptable(loud), "an entry the receiver would refuse is not acceptable");
    Entry empty;
    empty.op = Op::Flash;
    empty.site = siteOf(sites::Primitive::PlaySe);
    check(!wire::acceptable(empty), "an entry whose site is another op's is not acceptable");
}

}  // namespace

int main() {
    testSizes();
    testRoundTrip();
    testEncodeLimits();
    testHeaderRefusals();
    testEntryRefusals();
    testAcceptable();
    if (g_failures == 0) std::printf("weapon_fx_wire_test: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}
