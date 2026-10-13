# Script opcode and trigger coverage (substitute for the 165 named tables, which are not in the repo)

The "165 named tables" from RE0.md item 5 are not in any repo or on this PC. This doc uses the two real tables RE0_EVENT_SYNC.md names: the script opcode table at 0xcd57d8 (312 rows) and the trigger condition table at 0xd97368 (45 types). Names come from `tools/re0/bes2.py` (`opcode_table()`, image.bin). Trigger types have no names in the image; they are listed by number and handler.

Status:
- **synced**: the design or flag-sync code describes this row's co-op handling (evidence cited).
- **local-only**: the follower does not run this row; the leader does, or the row is a wait, a switch or a follow flag.
- **unknown**: no co-op handling recorded; the row runs on both machines by default and has not been reviewed.

Nothing here is live-tested. "synced" means the design and code say so, not that a play session confirmed it.

## Counts

| Table | Rows | synced | local-only | unknown |
|---|---|---|---|---|
| Script opcodes (0xcd57d8) | 312 | 21 | 86 | 205 |
| Trigger types (0xd97368) | 45 | 13 | 0 | 32 |

Opcode roles (event_rule.h `roleOf`): Run 226, LeaderOnly 71, Wait 10, Switch 3, Follow 2.

## Shared handler addresses

Rows that point at one handler address. Probably a shared stub or pass-through in the game; not checked op by op, so the rows keep their status above.

| handler | rows |
|---|---|
| 0x56fac0 | 0 nop, 180 ComingSoon, 200 MTHPPause, 201 MTHPContinue, 293 eff_prog, 306 ShowNotInstalled, 307 ReleaseScrManagerResouce |
| 0x56df80 | 11 call, 12 exec, 83 cinein2, 85 cineout2, 143 FadeColor, 181 FadeDispOrder, 230 PlayerDisp, 261 ResetAccessary |
| 0x57af30 | 82 cinein, 84 cineout, 256 cineinW, 257 cineoutW |
| 0x571620 | 190 SePop, 223 StopBGMSE |

## Top unknown opcodes for co-op sync

| opcode id | name | why it matters |
|---|---|---|
| 178 | EnemyDeathTypeAll | enemy-wide death; overlaps enemy sync, double kill risk |
| 192 | GameOver | GameOver ends the run on whichever machine runs it |
| 246 | PlayerDamage | PlayerDamage on the subject; damage must not be applied twice |
| 279 | PlayerMutekiSet | PlayerMutekiSet: invincibility on the subject only |
| 280 | PlayerMutekiReset | PlayerMutekiReset: paired with 279 |
| 37 | FlagSetMB | multi-bit story flag setter (FlagSetMB, calls 0x59c980 spawn path) |
| 38 | FlagSetF | float-valued story flag setter (FlagSetF) |
| 47 | SetLocalFlag | SetLocalFlag writes sEventScript (0xdcbebc), not the story bitset flag_sync diffs |
| 194 | ObjInvisible | object invisible by id; prop state not in the flags |
| 195 | ObjVisible | object visible by id; prop state not in the flags |
| 212 | ObjPosSet | object position set; prop state not in the flags |
| 241 | ObjPosSetOff | object position set with offset |
| 262 | AllObjInvisible | all objects invisible in the room |
| 263 | AllObjVisible | all objects visible in the room |
| 271 | ObjDeleteType | object delete by type |
| 202 | TimerSet | TimerSet: timers feed shared trigger conditions |
| 144 | TimeAttack | TimeAttack: timer state |
| 184 | BGMStop | BGMStop: BGM stopped on each machine |
| 222 | PlayBGMSE | PlayBGMSE: BGM and SE started on each machine |
| 238 | GetCostume | GetCostume / SetCostume / ChangeCostume (238-240): costume is per player |
| 266 | DispWeapon | DispWeapon: weapon shown on the subject |
| 267 | HideWeapon | HideWeapon: weapon hidden on the subject |

## (a) Script opcodes

| opcode id | name | sync handling | evidence file:line | status |
|---|---|---|---|---|
| 0 | nop | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 1 | frame | wait; the leader waits, the follower skips the wait | adapters/re0/src/event_rule.h:80 | local-only |
| 2 | wait | wait; the leader waits, the follower skips the wait | adapters/re0/src/event_rule.h:80 | local-only |
| 3 | wait_movie | wait; the leader waits, the follower skips the wait | adapters/re0/src/event_rule.h:80 | local-only |
| 4 | jmp | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 5 | exit | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 6 | je | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 7 | jl | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 8 | jg | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 9 | loop | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 10 | loopReg | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 11 | call | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 12 | exec | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 13 | gosub | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 14 | ret | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 15 | CmpResult | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 16 | CmpRegImm | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 17 | CmpLoopImm | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 18 | CmpResultFloat | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 19 | CmpResultFlag | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 20 | ResFlag | story guard flag read natively on each machine | docs/re/RE0_EVENT_SYNC.md:25 | synced |
| 21 | ResFlagMB | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 22 | ResFlagF | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 23 | ResReg | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 24 | ResLoop | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 25 | ResRand | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 26 | ResParam | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 27 | RegRes | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 28 | RegImm | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 29 | RegFlag | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 30 | RegFlagMB | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 31 | ParamFlagMB | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 32 | FlagParamMB | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 33 | RegFlagF | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 34 | ResSurfFront | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 35 | ResSurfFoot | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 36 | FlagSet | story flag set through the game setter; flag_sync carries the bit | docs/re/RE0_EVENT_SYNC.md:70 | synced |
| 37 | FlagSetMB | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 38 | FlagSetF | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 39 | SetEnemyFlag | enemy flag set through the setter; spawns the group on both machines | docs/re/RE0_EVENT_SYNC.md:135 | synced |
| 40 | GetEnemyFlag | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 41 | SetActFlag | act flag set through the setter; flag_sync redraws it on the peer | docs/RE0_NOTES.md:90 | synced |
| 42 | ActFlagCheck | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 43 | SetEffectFlag | effect flag set on both machines (the prop's look) | docs/re/RE0_EVENT_SYNC.md:97 | synced |
| 44 | SetEffectFlag2 | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 45 | SetBGMFlag | BGM flag set through the setter; flag_sync calls the BGM refresh | docs/RE0_NOTES.md:90 | synced |
| 46 | GetBGMFlag | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 47 | SetLocalFlag | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 48 | GetLocalFlag | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 49 | mes | message or selection screen; leader only, the follower takes the leader's branch | adapters/re0/src/event_rule.h:88 | local-only |
| 50 | mesSel | message or selection screen; leader only, the follower takes the leader's branch | adapters/re0/src/event_rule.h:88 | local-only |
| 51 | mes_common | message or selection screen; leader only, the follower takes the leader's branch | adapters/re0/src/event_rule.h:88 | local-only |
| 52 | mesSE | message or selection screen; leader only, the follower takes the leader's branch | adapters/re0/src/event_rule.h:88 | local-only |
| 53 | mes_commonSE | message or selection screen; leader only, the follower takes the leader's branch | adapters/re0/src/event_rule.h:88 | local-only |
| 54 | nomes | message or selection screen; leader only, the follower takes the leader's branch | adapters/re0/src/event_rule.h:88 | local-only |
| 55 | nomesEnd | message or selection screen; leader only, the follower takes the leader's branch | adapters/re0/src/event_rule.h:88 | local-only |
| 56 | door | door; leader only, door_sync carries it | adapters/re0/src/event_rule.h:90 | local-only |
| 57 | door_camera | door; leader only, door_sync carries it | adapters/re0/src/event_rule.h:90 | local-only |
| 58 | rumble | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 59 | rumb_stop | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 60 | item_get | inventory or item screen; leader only, inventory_sync carries the result | adapters/re0/src/event_rule.h:91 | local-only |
| 61 | item_check | inventory or item screen; leader only, inventory_sync carries the result | adapters/re0/src/event_rule.h:91 | local-only |
| 62 | item_getcheck | inventory or item screen; leader only, inventory_sync carries the result | adapters/re0/src/event_rule.h:91 | local-only |
| 63 | item_sub | inventory or item screen; leader only, inventory_sync carries the result | adapters/re0/src/event_rule.h:91 | local-only |
| 64 | key_check | inventory or item screen; leader only, inventory_sync carries the result | adapters/re0/src/event_rule.h:91 | local-only |
| 65 | SubItemJp | inventory or item screen; leader only, inventory_sync carries the result | adapters/re0/src/event_rule.h:92 | local-only |
| 66 | SubItemRet | inventory or item screen; leader only, inventory_sync carries the result | adapters/re0/src/event_rule.h:92 | local-only |
| 67 | anim_set | object animation runs on both machines | docs/re/RE0_EVENT_SYNC.md:97 | synced |
| 68 | anim_settype | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 69 | anim_settype2 | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 70 | anim_settypeend | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 71 | anim_settypeoff | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 72 | anim_settypeoffsetend | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 73 | anim_checktype | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 74 | anim_speed | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 75 | anim_WaitType | wait; the leader waits, the follower skips the wait | adapters/re0/src/event_rule.h:80 | local-only |
| 76 | anim_stop | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 77 | anim_continue | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 78 | obj_move | object move runs on both machines | docs/re/RE0_EVENT_SYNC.md:97 | synced |
| 79 | se | sound runs on both machines | docs/re/RE0_EVENT_SYNC.md:70 | synced |
| 80 | sewait | wait; the leader waits, the follower skips the wait | adapters/re0/src/event_rule.h:80 | local-only |
| 81 | save_point | save point; leader only | adapters/re0/src/event_rule.h:95 | local-only |
| 82 | cinein | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 83 | cinein2 | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 84 | cineout | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 85 | cineout2 | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 86 | UpCutStart | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 87 | UpCutEnd | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 88 | UpCutProc | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 89 | UpCutWorkSet | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 90 | ResUpCutWork | leader only | adapters/re0/src/event_rule.h:98 | local-only |
| 91 | UpCutWorkAdd | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 92 | UpCutWorkAddR | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 93 | UpCutWorkSub | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 94 | UpCutWorkSubR | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 95 | UCWorkLim | leader only | adapters/re0/src/event_rule.h:98 | local-only |
| 96 | UpCutEndWait | wait; the leader waits, the follower skips the wait | adapters/re0/src/event_rule.h:80 | local-only |
| 97 | UpCutDoor | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:90 | local-only |
| 98 | UpCutDoor_Camera | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:90 | local-only |
| 99 | UpCutAnimSet | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 100 | UpCutAnimSetEnd | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 101 | UpCutAnimSpeed | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 102 | UpCutWaitOff | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 103 | UpCutChange | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 104 | UpCutObjSet | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 105 | UpCutObjPut | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 106 | UpCutObjPutChg | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 107 | UpCutObjRot | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 108 | UpCutModelClr | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 109 | UpCutObjScale | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 110 | UpCutObjInvisible | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 111 | UpCutObjVisible | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 112 | UpCutEffectLoad | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 113 | UpCutEffectStart | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 114 | UpCutEffectEnd | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:98 | local-only |
| 115 | Sub | inventory or item screen; leader only, inventory_sync carries the result | adapters/re0/src/event_rule.h:92 | local-only |
| 116 | Sub_ReadyGet | inventory or item screen; leader only, inventory_sync carries the result | adapters/re0/src/event_rule.h:92 | local-only |
| 117 | Sub_ReadyGetParam | inventory or item screen; leader only, inventory_sync carries the result | adapters/re0/src/event_rule.h:93 | local-only |
| 118 | Sub_ReadySelect | inventory or item screen; leader only, inventory_sync carries the result | adapters/re0/src/event_rule.h:93 | local-only |
| 119 | Sub_ReadyTemp | inventory or item screen; leader only, inventory_sync carries the result | adapters/re0/src/event_rule.h:93 | local-only |
| 120 | Sub_GetResult | inventory or item screen; leader only, inventory_sync carries the result | adapters/re0/src/event_rule.h:93 | local-only |
| 121 | Sub_GetItem | inventory or item screen; leader only, inventory_sync carries the result | adapters/re0/src/event_rule.h:93 | local-only |
| 122 | SubCall | inventory or item screen; leader only, inventory_sync carries the result | adapters/re0/src/event_rule.h:94 | local-only |
| 123 | UpCutNoReload | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:87 | local-only |
| 124 | SubItem | inventory or item screen; leader only, inventory_sync carries the result | adapters/re0/src/event_rule.h:92 | local-only |
| 125 | CheckBgChange | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 126 | CharChange | character switch; run on neither machine, the lead passes to the other character | adapters/re0/src/event_rule.h:83 | local-only |
| 127 | CharChangeNC | character switch; run on neither machine, the lead passes to the other character | adapters/re0/src/event_rule.h:83 | local-only |
| 128 | CharChange2 | character switch; run on neither machine, the lead passes to the other character | adapters/re0/src/event_rule.h:83 | local-only |
| 129 | PlayerCheck | lift demo: checks the character on each machine | docs/re/RE0_EVENT_SYNC.md:110 | synced |
| 130 | SubCheck | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 131 | SubAreaCheck | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 132 | TraceOff | partner follow flag; run on neither machine, host sets party mode | adapters/re0/src/event_rule.h:85 | local-only |
| 133 | TraceOn | partner follow flag; run on neither machine, host sets party mode | adapters/re0/src/event_rule.h:85 | local-only |
| 134 | TraceCheck | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 135 | CamChange | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 136 | CamChange2 | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 137 | CamChangeNS | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 138 | EventParamSet | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 139 | FadeIn | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 140 | FadeIn_Wait | wait; the leader waits, the follower skips the wait | adapters/re0/src/event_rule.h:81 | local-only |
| 141 | FadeOut | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 142 | FadeOut_Wait | wait; the leader waits, the follower skips the wait | adapters/re0/src/event_rule.h:81 | local-only |
| 143 | FadeColor | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 144 | TimeAttack | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 145 | TimeAttackEnd | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 146 | EventProc | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 147 | EventProcWait | wait; the leader waits, the follower skips the wait | adapters/re0/src/event_rule.h:81 | local-only |
| 148 | SubMove | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 149 | CharPoint | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 150 | CharPointY | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 151 | CharAddY | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 152 | ChPosSet | player position set on the subject | docs/re/RE0_EVENT_SYNC.md:71 | synced |
| 153 | PlayerClimb | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 154 | PlayerClimb_Pos | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 155 | PlayerEventStart | lift demo: player event start on both machines | docs/re/RE0_EVENT_SYNC.md:110 | synced |
| 156 | CamChangeArea | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 157 | PlayerEquip | equip or weapon; leader only, equip_refresh carries it | adapters/re0/src/event_rule.h:95 | local-only |
| 158 | PlayerPause | lift demo: pauses the examining player | docs/re/RE0_EVENT_SYNC.md:109 | synced |
| 159 | PlayerContinue | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 160 | CharPause | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 161 | CharContinue | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 162 | CheckCamera | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 163 | EventTrigPause | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 164 | EventTrigCont | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 165 | EventPhaseIn | cutscene phase in on both machines | docs/re/RE0_EVENT_SYNC.md:133 | synced |
| 166 | EventPhaseOut | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 167 | Player_AnimSet | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 168 | Player_AnimSet2 | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 169 | Player_Reset | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 170 | Player_Reset2 | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 171 | Player_Anim_SetSpeed | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 172 | SpcSoundVolSet | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 173 | NeckRotation | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 174 | PlayerAddChild | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 175 | ObjectAddChild | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 176 | ClimbAddChild | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 177 | EnemyDeath | enemy death runs on both machines | docs/re/RE0_EVENT_SYNC.md:135 | synced |
| 178 | EnemyDeathTypeAll | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 179 | GoReset | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 180 | ComingSoon | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 181 | FadeDispOrder | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 182 | NotOnSomeBody | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 183 | PlayMovie | movie plays on both machines | docs/re/RE0_EVENT_SYNC.md:133 | synced |
| 184 | BGMStop | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 185 | InRoomCheck | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 186 | NotOnSomebody | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 187 | DmyDemoStart | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 188 | FenceRot | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 189 | SePush | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 190 | SePop | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 191 | ItemPut | inventory or item screen; leader only, inventory_sync carries the result | adapters/re0/src/event_rule.h:91 | local-only |
| 192 | GameOver | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 193 | YPosCheck | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 194 | ObjInvisible | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 195 | ObjVisible | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 196 | ObjInvisibleType | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 197 | ObjVisibleType | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 198 | ObjInvisibleTypeNo | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 199 | ObjVisibleTypeNo | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 200 | MTHPPause | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 201 | MTHPContinue | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 202 | TimerSet | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 203 | scene_disp_off | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 204 | scene_disp_on | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 205 | door_no_fade_out | door; leader only, door_sync carries it | adapters/re0/src/event_rule.h:90 | local-only |
| 206 | SubDelete | inventory or item screen; leader only, inventory_sync carries the result | adapters/re0/src/event_rule.h:94 | local-only |
| 207 | GetEffectFlag | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 208 | CamChkTypeReset | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 209 | CorrectPos | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 210 | OnCameraBit | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 211 | SceneCheck | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 212 | ObjPosSet | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 213 | mes_blend | message or selection screen; leader only, the follower takes the leader's branch | adapters/re0/src/event_rule.h:89 | local-only |
| 214 | obj_rotate | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 215 | UpCutSetkirari | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:87 | local-only |
| 216 | ResSurfFootType | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 217 | seloop | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 218 | seloopstop | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 219 | seloopcheck | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 220 | GetEffectFlagMB | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 221 | EventExec | fork; leader only, the fork arrives as its own start | adapters/re0/src/event_rule.h:95 | local-only |
| 222 | PlayBGMSE | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 223 | StopBGMSE | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 224 | DiskChange | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 225 | SetDisplayFlag | display flag set through the setter; flag_sync redraws it on the peer | docs/RE0_NOTES.md:90 | synced |
| 226 | Demo_EntryPos | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 227 | Demo_Start | cutscene demo start on both machines | docs/re/RE0_EVENT_SYNC.md:133 | synced |
| 228 | SoundLoad | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 229 | UpCutEndDoff | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:87 | local-only |
| 230 | PlayerDisp | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 231 | PlayerAnimPause | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 232 | SetEffectColor | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 233 | AfterDemo | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 234 | UpCutEnd_unreset | close-up screen; leader only, the choice stays on the leader's screen | adapters/re0/src/event_rule.h:87 | local-only |
| 235 | ChgSubCamera | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 236 | AfterDemo2 | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 237 | OmakeEnd | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 238 | GetCostume | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 239 | SetCostume | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 240 | ChangeCostume | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 241 | ObjPosSetOff | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 242 | BgSePause | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 243 | BgSeContinue | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 244 | FenceRotCheck | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 245 | player_check_animno | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 246 | PlayerDamage | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 247 | EnablePad | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 248 | DisablePad | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 249 | mesEnd | message or selection screen; leader only, the follower takes the leader's branch | adapters/re0/src/event_rule.h:89 | local-only |
| 250 | wait_cancel | wait; the leader waits, the follower skips the wait | adapters/re0/src/event_rule.h:81 | local-only |
| 251 | SE_StopAll | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 252 | NotOnBoth | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 253 | FenceInit | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 254 | RumbleStop | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 255 | OmakeStart | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 256 | cineinW | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 257 | cineoutW | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 258 | ObjAddY | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 259 | SceneCheck2 | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 260 | seloop_volume | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 261 | ResetAccessary | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 262 | AllObjInvisible | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 263 | AllObjVisible | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 264 | CutChangePause | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 265 | CutChangeContinue | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 266 | DispWeapon | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 267 | HideWeapon | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 268 | AllAreaObjInvisible | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 269 | AllAreaObjVisible | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 270 | AfterDemoNC | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 271 | ObjDeleteType | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 272 | SubItemRet2 | inventory or item screen; leader only, inventory_sync carries the result | adapters/re0/src/event_rule.h:92 | local-only |
| 273 | EventCamCheck | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 274 | SceneEvalPause | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 275 | SceneEvalContinue | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 276 | CorrectPos2 | partner position correction on both machines | docs/re/RE0_EVENT_SYNC.md:110 | synced |
| 277 | ControlPause | ControlPause runs on both machines (player's own character) | docs/re/RE0_EVENT_SYNC.md:72 | synced |
| 278 | ControlContinue | ControlContinue runs on both machines | docs/re/RE0_EVENT_SYNC.md:28 | synced |
| 279 | PlayerMutekiSet | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 280 | PlayerMutekiReset | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 281 | MemoryWeapon | equip or weapon; leader only, equip_refresh carries it | adapters/re0/src/event_rule.h:95 | local-only |
| 282 | CompWeapon | equip or weapon; leader only, equip_refresh carries it | adapters/re0/src/event_rule.h:95 | local-only |
| 283 | Alone | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 284 | ForceBGMCall | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 285 | PlayerPause2 | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 286 | PlayerContinue2 | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 287 | anim_stop2 | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 288 | anim_continue2 | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 289 | SubPause | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 290 | SubContinue | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 291 | anim_stopSub | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 292 | anim_continueSub | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 293 | eff_prog | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 294 | ResSurfFootFront | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 295 | anim_stopSub2 | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 296 | anim_continueSub2 | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 297 | EnvBgmSet | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 298 | DeathCheck | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 299 | DoorCancel | door; leader only, door_sync carries it | adapters/re0/src/event_rule.h:90 | local-only |
| 300 | ChunkCheck | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 301 | RequestAchievement | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 302 | mes_dialog | message or selection screen; leader only, the follower takes the leader's branch | adapters/re0/src/event_rule.h:89 | local-only |
| 303 | PlayMovieOldBG | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 304 | UseKey | inventory or item screen; leader only, inventory_sync carries the result | adapters/re0/src/event_rule.h:91 | local-only |
| 305 | InvalidStartPause | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 306 | ShowNotInstalled | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 307 | ReleaseScrManagerResouce | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 308 | StartCutChangeFilter | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 309 | NoChangeCutChangeFilter | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 310 | EndCutChangeFilter | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |
| 311 | CheckBgChange2 | runs on both machines by default; no sync review recorded | adapters/re0/src/event_rule.h:98 | unknown |

## (b) Trigger types

| trigger type | name | sync handling | evidence file:line | status |
|---|---|---|---|---|
| 0 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x60ab60 | unknown |
| 1 | not named in repo | local: fires only where its own character met it | adapters/re0/src/event_rule.h:29, handler 0x563f40 | synced |
| 2 | not named in repo | local: fires only where its own character met it | adapters/re0/src/event_rule.h:29, handler 0x564070 | synced |
| 3 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x564190 | unknown |
| 4 | not named in repo | local when p2 = 0x20000 (the controlled character), otherwise shared: the room authority fires it while the peer is in the room | adapters/re0/src/event_rule.h:31, handler 0x564200 | synced |
| 5 | not named in repo | local when p2 = 0x20000 (the controlled character), otherwise shared: the room authority fires it while the peer is in the room | adapters/re0/src/event_rule.h:31, handler 0x564410 | synced |
| 6 | not named in repo | local: fires only where its own character met it | adapters/re0/src/event_rule.h:29, handler 0x564620 | synced |
| 7 | not named in repo | local: fires only where its own character met it | adapters/re0/src/event_rule.h:29, handler 0x564730 | synced |
| 8 | not named in repo | local: fires only where its own character met it | adapters/re0/src/event_rule.h:29, handler 0x564840 | synced |
| 9 | not named in repo | local: fires only where its own character met it | adapters/re0/src/event_rule.h:29, handler 0x564940 | synced |
| 10 | not named in repo | local: fires only where its own character met it | adapters/re0/src/event_rule.h:29, handler 0x564af0 | synced |
| 11 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x564c00 | unknown |
| 12 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x564d00 | unknown |
| 13 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x564e60 | unknown |
| 14 | not named in repo | either character in zone: shared; the room authority fires it | docs/re/RE0_EVENT_SYNC.md:23, handler 0x564f40 | synced |
| 15 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x565070 | unknown |
| 16 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x5651a0 | unknown |
| 17 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x565300 | unknown |
| 18 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x5653c0 | unknown |
| 19 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x565510 | unknown |
| 20 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x565700 | unknown |
| 21 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x5658f0 | unknown |
| 22 | not named in repo | local: fires only where its own character met it | adapters/re0/src/event_rule.h:29, handler 0x565a20 | synced |
| 23 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x565bb0 | unknown |
| 24 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x565cd0 | unknown |
| 25 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x565df0 | unknown |
| 26 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x565f80 | unknown |
| 27 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x566230 | unknown |
| 28 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x5664e0 | unknown |
| 29 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x566570 | unknown |
| 30 | not named in repo | local: fires only where its own character met it | adapters/re0/src/event_rule.h:29, handler 0x566600 | synced |
| 31 | not named in repo | local: fires only where its own character met it | adapters/re0/src/event_rule.h:29, handler 0x566790 | synced |
| 32 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x5668f0 | unknown |
| 33 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x566a10 | unknown |
| 34 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x566b80 | unknown |
| 35 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x566d70 | unknown |
| 36 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x566e30 | unknown |
| 37 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x566f70 | unknown |
| 38 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x567150 | unknown |
| 39 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x567260 | unknown |
| 40 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x5673f0 | unknown |
| 41 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x5675c0 | unknown |
| 42 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x567850 | unknown |
| 43 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x567a20 | unknown |
| 44 | not named in repo | shared by default (no explicit class); handler not read | adapters/re0/src/event_rule.h:33 (default), handler 0x567c00 | unknown |
