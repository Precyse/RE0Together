#pragma once
// DS2-internal: finds who queues a message for a null entity. The engine's deferred message queue is flushed through
// 0x14104E130, which dispatches each message to the entity at message +0x48; a null one faulted at 0x140136E9C (an
// intermittent crash at the peer's connect). Before each flush the lists are scanned and a message without an entity is logged
// (its class, the flush, whether the remote body is live, parked, spawning or absent); the flush itself runs unchanged.
namespace queue_diag {

// Start-up, before the world loads.
void installEarly();

}  // namespace queue_diag
