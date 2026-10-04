#pragma once
// DS2-internal: test commands for live checks (adapter.ini test_commands=1), see ds2/test_commands.cpp.
namespace test_commands {

// Start-up: the simulation tick that runs the command files, and the fault logger.
void installEarly();

}  // namespace test_commands
