#pragma once

namespace join_trace {

// Read-only logging of the session and join events of a co-op join, to learn their order and the limits in play.
// Calls that repeat are logged when their result changes.
void install();

}  // namespace join_trace
