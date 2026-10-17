#pragma once
#include <cstdint>

// The content hash of the version gate (content_hash_rules.h defines the byte stream): the adapter's own dinput8.dll and
// every file of <game>\coop\scripts.
namespace content_hash {

// Reads the files named by script_override::names(), hashes them once and logs the value. Run on the init thread after
// script_override::scan and before version_gate::install. A file that cannot be read is logged and still counts.
uint64_t compute();

}  // namespace content_hash
