// DEATH STRANDING 2: baggage owners link to each other by hash, not by player: a carrier's own identifier (owner +0x20)
// and its parent's (owner +0x38) are hashes taken from its resource, and registering an owner adopts every existing
// owner whose parent hash equals the new owner's identifier. The remote body is built from Sam's entity resource, so
// its owner has Sam's identifier and registering it re-parented Sam's backpack and pouch owners to the remote (and its
// own backpack went under Sam's). Here the carriers created for the remote get hashes of their own before they register:
// the remote's owner a new identifier, and its backpack a new parent hash that names it.
#include "ds2/remote_baggage.h"

#include "decima/safe_read.h"
#include "ds2/engine.h"
#include "ds2/remote_camera.h"
#include "hooks.h"
#include "log.h"

namespace {

constexpr uintptr_t kCarrierAdded = 0x14123bad0;  // DSBaggageCarrierComponent: MsgAddedToEntityManager (component, message)
constexpr uintptr_t kComponentResource = 0x30;
constexpr uintptr_t kComponentOwnId = 0x68;       // 0: use the resource's identifier
constexpr uintptr_t kComponentParentId = 0x6C;    // 0: use the resource's parent
constexpr uintptr_t kResourceIdentifier = 0x20;
constexpr uintptr_t kResourceParent = 0x28;
constexpr uintptr_t kBaggageManagerGlobal = 0x14623EA48;
constexpr uintptr_t kManagerLocalOwner = 0x24288;  // the local player's baggage owner
constexpr uintptr_t kOwnerIdentifier = 0x20;
constexpr uint32_t kRemoteHash = 0x5EE70001;  // an identifier no resource carries

using HandlerFn = void (*)(uintptr_t component, uintptr_t message);

HandlerFn g_original = nullptr;

uint32_t localIdentifier() {
    const uintptr_t manager = decima::readPointer(ds2::at(kBaggageManagerGlobal));
    const uintptr_t owner = manager ? decima::readPointer(manager + kManagerLocalOwner) : 0;
    uint32_t identifier = 0;
    return owner && decima::safeRead(owner + kOwnerIdentifier, identifier) ? identifier : 0;
}

void carrierAdded(uintptr_t component, uintptr_t message) {
    if (remote_camera::spawning()) {  // the carriers the engine builds while this thread spawns the remote are the remote's
        const uintptr_t resource = decima::readPointer(component + kComponentResource);
        const uint32_t local = localIdentifier();
        uint32_t identifier = 0, parent = 0, ownId = 0, parentId = 0;
        decima::safeRead(resource + kResourceIdentifier, identifier);
        decima::safeRead(resource + kResourceParent, parent);
        decima::safeRead(component + kComponentOwnId, ownId);
        decima::safeRead(component + kComponentParentId, parentId);
        if (local && ownId == 0 && identifier == local) {
            ds2::field<uint32_t>(component, kComponentOwnId) = kRemoteHash;
            logger::write("remote_baggage: the remote's own owner gets its own identifier");
        } else if (local && parentId == 0 && parent == local) {
            ds2::field<uint32_t>(component, kComponentParentId) = kRemoteHash;
            logger::write("remote_baggage: a carrier of the remote is parented to the remote's owner, not Sam's");
        }
    }
    g_original(component, message);
}

}  // namespace

namespace remote_baggage {

void installEarly() {
    hooks::install("baggage carrier registration", ds2::at(kCarrierAdded), reinterpret_cast<void*>(&carrierAdded),
                   reinterpret_cast<void**>(&g_original));
}

}  // namespace remote_baggage
