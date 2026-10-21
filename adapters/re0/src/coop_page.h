#pragma once

// The Co-op page of the Options screen: a read-only status page (partner, link, mode, character, outfit, host) and its
// Back row, drawn by the overlay from coop_page_view. All calls are on the game thread, from the Options router's move
// (options_coop.cpp).
namespace coop_page {

// The page opens: publishes the first frame of rows.
void open();

// Every frame while the page is open: refreshes the rows and reads the menu input the way the game's own screens do.
// True when the player confirmed or cancelled, so the page should close.
bool update();

// The page is gone: the overlay stops drawing it.
void close();

}  // namespace coop_page
