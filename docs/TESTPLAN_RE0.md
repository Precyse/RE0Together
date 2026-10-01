# RE0 co-op test pass

Run with two players on the latest release. F8 shows the counters named below.

**Before you start:** restart the host launcher, so the guest's adapter log reaches the host as `coop\peer_<steamid>.log`.

| # | Do | Expect | F8 / log |
| --- | --- | --- | --- |
| 1 | Host in TEAM, same room, goes through a door | door animation on both screens, both arrive in the new room, Billy beside Rebecca | doors sent/run/blocked: host sent +1, guest run +1; `door_sync: ran the peer's door` in the guest log |
| 2 | Guest walks Billy to a door, presses action | camera moves to Billy on both screens, both go through | guest sent +1; `acted on a trigger as the partner` |
| 3 | E (LEAVE_BEHIND), then go through a door | the other character stays behind | party mode = leave behind |
| 4 | Guest opens the inventory while Rebecca has the camera | Billy's inventory is shown; the camera returns to Rebecca on close | `menu_mirror: menu opened for Billy` |
| 5 | Exchange an item Billy to Rebecca from the guest's menu | both screens show the item on Rebecca after the menu closes | inventory exchanges +1 on the guest, inventory applied on the host |
| 6 | Unlock a door / use a key item / pick up a story item | the other machine shows the same world state after re-entering the room | flag words sent/applied |
| 7 | Open the map, read a note, or save at a typewriter | the other player's world waits ("Waiting for partner") | world frozen = yes |
| 8 | Trigger a cutscene; one player skips | the skipper waits until the other finishes | phase lines in the log (`phase: Main -> EventDemo`) |
| 9 | Host saves at a typewriter | the guest's launcher logs `Save sync: received data0.bin` | host log `save data0.bin written, sharing it` |
| 10 | A character dies, both continue | both reload the host's last save | phase `-> Dead` on both |
| 11 | Split up (LEAVE_BEHIND, separate rooms), press V | both screens zap to the other character and load its room | `phase: Main -> Change` on both |
| 12 | Guest starts the game after the host is already playing (any room) | guest goes through the menus by itself (keys ignored), loads the host's slot, then is teleported into the host's room with the host's inventories | `auto_join:` lines, `session_slot: load of slot N turned into the host's slot`, `join_sync: teleporting`, `join_sync: in the host's room` |
| 13 | Die, host picks Continue | guest waits muted on game over, then continues by itself into the host's save | `auto_join: waiting for the host to be in game` |
| 14 | Play with some lag (Wi-Fi) | Billy's input stays smooth; the target settles | pad buffer/target, pad underruns |
| 15 | Host saves at a typewriter, picking slot 1 | the save lands in slot 20; slot 1 is unchanged | `session_slot: save to slot 0 kept in the co-op slot 19` |
| 16 | Guest pulls their network cable for ~20 s, then reconnects | guest launcher rejoins by itself, guest snaps back into the host's room | `Rejoining lobby`, `Rejoined the session`, `join_sync:` lines |
| 17 | Guest on an older build joins | guest launcher refuses with the build numbers | `Build mismatch:` |
| 18 | Guest leaves mid-game | the host's Billy returns to partner AI (not driven by the host's keys) | `partner_think: restored partner AI` |
| 19 | Both set `split_rooms=1` in `coop\adapter.ini`. E (LEAVE_BEHIND), host takes a door alone | host's screen follows Rebecca; the guest's screen stays on Billy in the old room after a short zap there and back | `split_rooms: replaying`, `split_rooms: door replayed` in the guest log |
| 20 | Apart (row 19), each player walks around and fights | each camera stays on its own character; enemies react in both rooms | no `Room desync` toast |
| 21 | Apart, the guest walks Billy through a door into Rebecca's room | both characters in the same room on both screens, back to normal co-op | `split_rooms: door replayed` in the host log |

Report any row that fails, together with both logs.
