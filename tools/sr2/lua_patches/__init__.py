"""The patch data for the Lua scripts of Saints Row 2, one module per group of scripts.
PATCHES maps a script's file name to its lua_patch.Patch."""
from . import library, missions_bh, missions_ep, missions_rn, missions_ss, missions_tss, stronghold
from .generic import RULES as GENERIC_RULES

PATCHES = {}
for group in (library, stronghold, missions_bh, missions_ep, missions_rn, missions_ss, missions_tss):
    PATCHES.update(group.PATCHES)

__all__ = ["PATCHES", "GENERIC_RULES"]
