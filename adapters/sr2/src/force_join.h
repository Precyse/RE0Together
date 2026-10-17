#pragma once

// Milestone 6: the launcher hands the adapter a session through <game dir>\coop\sr2_join.txt and the game connects itself
// through its own co-op browser (docs/SR2_NOTES.md "Force join"). The host's session name carries the token `cf<lobby id>`
// and is public with the launcher's password; the guest searches the browser for the token and joins the host it finds.
// Nothing happens without a valid join file.
namespace force_join {

// Checks the six sites, then queues the hook of the front end tick (inside a hooks batch) and starts the session
// monitor. Nothing is changed when a site differs. `trace` logs every step. True when installed.
bool install(bool trace);

}  // namespace force_join
