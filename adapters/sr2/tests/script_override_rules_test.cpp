// The script override rule (no game): which file name has an override and what path the game then opens.
#include <cstdio>

#include "../src/script_override_rules.h"
#include "check.h"

int main() {
    using script_override_rules::fileNameOf;
    using script_override_rules::overridePath;

    const std::set<std::string> names = {"ug_lib.lua", "sh_bh_docks.lua"};
    check(fileNameOf("ug_lib.lua") == "ug_lib.lua" && fileNameOf("scripts/ug_lib.lua") == "ug_lib.lua" &&
              fileNameOf("data\\missions\\sh_bh_docks.lua") == "sh_bh_docks.lua" && fileNameOf("dir/").empty(),
          "the file name is the last path component");
    check(overridePath("ug_lib.lua", names) == "coop\\scripts\\ug_lib.lua", "a bare name with an override opens coop scripts");
    check(overridePath("some\\dir\\sh_bh_docks.lua", names) == "coop\\scripts\\sh_bh_docks.lua",
          "a path is replaced by the coop scripts folder and the file name");
    check(overridePath("UG_LIB.LUA", names) == "coop\\scripts\\UG_LIB.LUA", "the match ignores case");
    check(overridePath("system_lib.lua", names).empty() && overridePath("", names).empty() && overridePath("dir/", names).empty(),
          "no override: the path stays");
    check(overridePath("ug_lib.lua", {}).empty(), "no overrides at all: the path stays");
    return g_failures == 0 ? 0 : 1;
}
