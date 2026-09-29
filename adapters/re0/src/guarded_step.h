#pragma once

// Runs one step of a command path so that a fault cannot take the command feature down for the session.
// A C++ exception or an SEH fault is logged, `reset` restores the step's state, and false is returned; the next
// call runs normally.
bool guardedStep(const char* name, void (*step)(), void (*reset)());
