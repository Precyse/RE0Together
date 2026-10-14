#pragma once
#include <cstdint>

// Milestone 5i, C1: the ambient population spawner (0x90D430) walks the spawn group's elements and read a null one (a
// crash at 0x90D44E on a guest). The element or its template being null now skips the element, as an element that does not
// spawn anything. Only a guard: where the null comes from is not confirmed (docs/SR2_NOTES.md, "Looks").
namespace ambient_guard {

// Under trace=1 each skip is logged, at most one line per second.
void setTrace(bool trace);

// C1: the load of an element and its template jumps here (ebp = the element pointer slot, edi = the element index).
uintptr_t skipNullElementThunk();

}  // namespace ambient_guard
