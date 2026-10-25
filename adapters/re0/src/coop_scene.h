#pragma once
#include <optional>
#include <string>

// The two characters drawn beside the Co-op page (docs/RE0_NOTES.md "Co-op HUD"): what coop_menu.cpp publishes each time
// the page refreshes and the overlay (hud_coop_scene.cpp) draws. A scene the page stopped refreshing is no longer there.
// Any thread.
namespace coop_scene {

// One player's side. An empty picture and name: nothing to draw.
struct Side {
    std::string picture;  // the outfit whose picture is drawn (coop\ui\outfits\<picture>.png)
    std::string name;     // the character's name
    std::string role;     // the line under the name
    bool open = false;    // nobody sits here: the picture is a pulsing silhouette and the texts are dimmed
};

struct Scene {
    Side host;   // drawn on the left
    Side guest;  // drawn on the right
};

void publish(const Scene& scene);

// The newest scene, if the page refreshed it lately.
std::optional<Scene> current();

}  // namespace coop_scene
