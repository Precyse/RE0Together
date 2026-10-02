#pragma once
// DS2-internal: the game's FactDatabase holds the world's story, order and progress state as named facts (bool, integer,
// float). The exported setters forward to writers taking the database, the fact UUID and the value. The
// host's world is the truth, so these writes are what the guest's world must follow (docs/DS2_NOTES.md, "Stage C").
namespace world_facts {

// Start-up: hooks the fact setters. With `log` every write is logged (context, fact UUID, value) for mapping which
// facts an action changes.
void installEarly(bool log);

}  // namespace world_facts
