"""Rules applied to every story mission script after its own edits: the forms of the two-player code that always
change the same way. Each rule is (regular expression, replacement)."""

NAME = r"[A-Za-z_][A-Za-z0-9_.]*"
VALUE = r"\{\}|[^,{}\[\]]+?"

RULES = [
    # name == REMOTE_PLAYER, REMOTE_PLAYER == name
    (rf"\b({NAME})\s*==\s*REMOTE_PLAYER\b", r"coop_is_remote_player(\1)"),
    (rf"\bREMOTE_PLAYER\s*==\s*({NAME})\b", r"coop_is_remote_player(\1)"),
    # name ~= REMOTE_PLAYER, REMOTE_PLAYER ~= name
    (rf"\b({NAME})\s*~=\s*REMOTE_PLAYER\b", r"not coop_is_remote_player(\1)"),
    (rf"\bREMOTE_PLAYER\s*~=\s*({NAME})\b", r"not coop_is_remote_player(\1)"),
    # { [LOCAL_PLAYER] = a, [REMOTE_PLAYER] = b }
    (
        rf"\{{\s*\[LOCAL_PLAYER\]\s*=\s*({VALUE})\s*[,;]\s*\[REMOTE_PLAYER\]\s*=\s*({VALUE})\s*[,;]?\s*\}}",
        r"coop_player_table(\1, \2)",
    ),
    # state[LOCAL_PLAYER] and state[REMOTE_PLAYER], state[LOCAL_PLAYER] or state[REMOTE_PLAYER]
    (rf"\b({NAME})\[LOCAL_PLAYER\]\s+and\s+\1\[REMOTE_PLAYER\]", r"coop_all_players_in(\1)"),
    (rf"\b({NAME})\[LOCAL_PLAYER\]\s+or\s+\1\[REMOTE_PLAYER\]", r"coop_any_player_in(\1)"),
]
