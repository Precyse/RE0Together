"""What the patches share about the per-remote slots of tools/sr2/lua/coop_slots.lua."""

# The HUD slots (bar, timer, objective) of the remote players #PLAYER2# to #PLAYER4#, as a Lua table.
REMOTE_HUD_SLOTS = "{ 1, 2, 3 }"


def reserve_seats(number, call, vehicle, seats):
    """The line edit (line number, find, replace) that reserves `seats` of `vehicle`, the seats the script gives the local
    player and the characters around the remote, on the same line as the remote's seating `call`."""
    reservation = f"coop_reserve_seats({vehicle}, {{ {', '.join(str(seat) for seat in seats)} }}) "
    return (number, call, reservation + call)
