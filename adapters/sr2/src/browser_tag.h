#pragma once
#include <string>

// Milestone 11 B3: the online browser lists only hosts the version gate would admit. The tag `cfv<VVV>-<digest>`
// (trailer_rules::tagFor) is part of the host's session name (the QR2 key online_name), and the browser's name filter holds
// it unless the player typed another one, so the co-op query asks for `online_name LIKE '%tag%'`. Both are re-asserted on
// every front end tick, after Juiced writes the name once when it logs in. The handlers' gates stay the enforcement: a LAN
// game or a typed filter does not use the tag.
namespace browser_tag {

// Starts tagging with `tag`. Before the front end tick hook is enabled.
void enable(const std::string& tag);

// One front end tick: puts the tag into the session name when it is missing, and into the filter when that is empty.
void tick(bool trace);

}  // namespace browser_tag
