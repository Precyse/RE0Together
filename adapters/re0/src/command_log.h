#pragma once

// Observability of the party commands (party mode): every press, request and host decision goes
// to adapter.log and the panel. Any thread. Identical lines repeated within a second are logged once.
namespace command_log {

// A detected local press from `source` ("keyboard" or "controller"); also the panel's "last command".
void press(const char* name, const char* source);

// A request sent, received or routed, or any other step worth a log line.
void note(const char* format, ...);

// What a request ended in (applied, or ignored with the reason); also the panel's "last decision".
void decide(const char* format, ...);

}  // namespace command_log
