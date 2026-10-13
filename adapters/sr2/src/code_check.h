#pragma once

// Read-only check that the running exe is the build the fixed addresses were taken from: the PE time stamp, the
// prologue of every function the adapter hooks (a patch that hooked one first shows up here) and the two limit
// stores and the two co-op pool size immediates. Logs each mismatch. Nothing is patched when it returns false.
bool gameMatchesBuild();
