"""One module per TESTPLAN_STATE test. Each defines ID, ROWS (the test plan rows it covers), SEATS (the fake's seats it
supports), SCRATCH_SEATS (the seats that change the save's state and need --scratch), SUMMARY, `gates(seat)` and `run(ctx)`."""
from fake_peer.scenarios import (record, t1_input, t2_echo, t4_join, t5_observe, t5_observer, t6_authority, t7_flags,
                                 t8_contact, t8_victim, t9_rooms, t10_props, t11_boss, t12_items, t13_death_save, t14_link)

# Order of first use: each run fills the library for the ones after it.
ORDER = (t2_echo, t4_join, t9_rooms, t5_observe, t8_contact, t12_items, t10_props, t7_flags, t13_death_save, t11_boss,
         t14_link, t1_input, t5_observer, t6_authority, t8_victim, record)
REGISTRY = {module.ID: module for module in ORDER}
