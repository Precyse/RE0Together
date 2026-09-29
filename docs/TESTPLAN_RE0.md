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
| 12 | Guest leaves mid-game | the host's Billy returns to partner AI (not driven by the host's keys) | `partner_think: restored partner AI` |

Report any row that fails, together with both logs.
