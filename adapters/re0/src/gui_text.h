#pragma once

// Hook of the game's one text lookup, rGUIMessage::getString 0x887090: every label, help line and dialog text of the
// GUI goes through it. A (table, id) pair listed in gui_text_rule.h is answered with our string, anything else runs the
// game's own lookup.
namespace gui_text {

// Installs the hook after checking the site's bytes. Call after the game code is decrypted; false when the site does
// not match this build or the hook failed (the game's text is then untouched).
bool enable();

void uninstall();

}  // namespace gui_text
