#pragma once

// Redirects one of the game's own imports: the game executable's import slot for `function` from `dll` is pointed at
// `detour`, so only the game's calls are affected (not other modules'). No library is loaded, so it is safe under the
// loader lock. Returns the original target, or nullptr when the game does not import it.
namespace import_patch {

void* redirect(const char* dll, const char* function, void* detour);

}  // namespace import_patch
