#pragma once

// Milestone 5b: read-only trace of the join path across machines, to see where a third player's join stops
// (docs/SR2_NOTES.md, "Join trace"). Host: the NAT request and its result, the join_req dispatch and handler (with the
// silent drops), the deny reasons, the member add result and the member allocation. Joiner: join_req sent, join_deny
// and join_acc received. Every line starts with `trace:`.
namespace join_net_trace {

// Checks all sites, then writes the two drop-branch jumps and queues the entry hooks (inside a hooks batch). Nothing is
// changed when a site differs. True when installed.
bool install();

}  // namespace join_net_trace
