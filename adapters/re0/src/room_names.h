#pragma once
#include <cstdint>

// English room names by scene id, for the partner tag (pure, no game, unit tested).
//
// A scene id is its stage's first id plus the room number of the room's file name (sh0e = 0x24 + 0x0e = 0x32), from
// the game's own tables: stage starts at 0xdb0b70 (read by Scene_stageIndex 0x61a520, Scene_indexInStage 0x61a4a0) and
// stage prefixes at 0xd70e5c, joined by "Room%s%02x" (0x408850): Tr 0x00, Sh 0x24, Dm 0x56, Ch 0x63, Bt 0x84,
// Pl 0x8d, last 0xae (the "id <= SCENE_LAST" assert). The map shows no room names (ui\30_map map_text00_eng holds only
// the area names Train, Training Facility, Facility Basement, Laboratory, Factory, Treatment Plant), so:
//  - the twelve typewriter rooms carry the game's own save point names (message_savepoint_eng, index from each room
//    script's save_point operand);
//  - every other room carries its Resident Evil Wiki name (residentevil.fandom.com, Ecliptic Express, Umbrella
//    Executive Training School, Laboratory, Factory and Arklay treatment plant room pages, after biohazard 0
//    KAITAISHINSHO), matched to the id by its items and door links in the Resident Evil 0 Randomizer's room data
//    (github.com/HansRandomizer/Resident-Evil-0-Randomizer) and the door order of the re0hd LiveSplit autosplitter.
// Ids with no confirmed room (cutscene, elevator and alternate rooms) are left out.
namespace room_names {

struct Room {
    uint16_t scene;
    const char* name;
};

inline constexpr Room kRooms[] = {
    // Train
    {0x00, "Operator Cab"},
    {0x01, "Exterior Corridor"},
    {0x02, "Second Car Passage"},
    {0x03, "Conductor's Room"},
    {0x04, "Cabin"},
    {0x05, "Second Class Private Room B"},
    {0x06, "Second Class Passenger Car B"},
    {0x07, "Second Class Passenger Car A"},
    {0x08, "Dining Car"},
    {0x09, "Cargo Compartment"},
    {0x0a, "Rear Deck"},
    {0x0b, "Saloon Car"},
    {0x0c, "Third Car Passage"},
    {0x0d, "First Class Private Room A"},
    {0x0e, "First Class Private Room B"},
    {0x0f, "Dining Car 2F"},
    {0x11, "Pantry"},
    // Training Facility
    {0x24, "Hall"},
    {0x25, "Cafeteria"},
    {0x26, "Kitchen"},
    {0x27, "Operations Control Room"},
    {0x28, "Operations Control Room Hallway"},
    {0x29, "Restroom"},
    {0x2a, "Boiler Room"},
    {0x2b, "Observatory"},
    {0x2c, "Storeroom"},
    {0x2d, "Garden"},
    {0x2f, "Conference Room"},  // the wiki's Assembly hall
    {0x30, "Library Hallway"},
    {0x31, "Infirmary"},
    {0x32, "Library"},
    {0x33, "Faculty Room"},
    {0x34, "Archive"},
    {0x35, "Director's Office Hallway"},
    {0x36, "Director's Office"},
    {0x37, "Drawing Room"},
    {0x38, "Meeting Room"},  // the wiki's Conference room (kaigishitsu); the game gives that name to 0x2f
    {0x39, "Art Storeroom"},
    {0x3a, "Bar Hallway"},
    {0x3b, "Laboratory Hallway"},
    {0x3c, "Gas Laboratory"},
    {0x3d, "Engineering Workshop"},
    {0x3e, "Bar"},
    {0x3f, "Broadcasting Room"},
    {0x40, "Terrace"},
    {0x41, "Rearing Pool"},
    {0x42, "Tunnel"},
    {0x43, "Sewer"},
    // Facility Basement
    {0x56, "Monitor Room Corridor"},
    {0x57, "Monitor Room"},
    {0x58, "Torture Chamber"},
    {0x59, "Observation Room"},
    {0x5a, "Basement Hall"},
    {0x5b, "Basement 2F"},  // the wiki's Worksite ruins
    {0x5c, "Dormitory B"},
    {0x5d, "Dormitory A"},
    {0x5e, "Storage Yard"},
    {0x5f, "Training Facility Hallway"},
    {0x60, "Training Facility"},
    {0x61, "Confinement Room"},
    // Laboratory
    {0x63, "Islet"},
    {0x64, "Chapel"},
    {0x65, "Chapel Roof"},
    {0x66, "Platform"},
    {0x67, "Hallway Atrium"},
    {0x68, "Machine Room"},
    {0x69, "Marcus' Laboratory"},
    {0x6a, "Collection Room"},
    {0x6b, "Operating Room"},
    {0x6c, "Morgue"},
    {0x6d, "Breeding Room"},
    {0x6e, "Auxiliary Research Room"},
    {0x6f, "Ropeway"},
    {0x70, "Library"},  // the wiki's Reference room
    {0x71, "Marcus' Boudoir"},
    {0x72, "Church"},  // the wiki's Private chapel room
    // Factory
    {0x84, "Platform B"},
    {0x86, "Marshalling Yard"},
    {0x87, "Monitor Room Passage"},
    {0x88, "Panel Room"},
    // Treatment Plant
    {0x8d, "Treatment Plant Entry"},
    {0x8e, "Control Room"},
    {0x8f, "Upper Pump Room"},
    {0x90, "Lower Pump Room"},
    {0x91, "B6 Passage A"},
    {0x92, "Restroom"},
    {0x93, "B6 Passage B"},
    {0x94, "Lodgings"},  // the wiki's Treatment plant dormitory
    {0x96, "Purification Room"},
    {0x97, "Purification Room Passage"},
    {0x98, "Power Supply and Air Conditioning Room"},
    {0x99, "Yard"},
    {0x9a, "Water Tank Room"},
    {0x9b, "Water Tank Room Passage"},
    {0x9c, "Reservoir Room"},
    {0x9d, "No.1 Laboratory"},
    {0x9e, "Dam Control Tower (Top)"},
    {0x9f, "B8 Final Processing Room"},
    {0xa0, "B9 Final Processing Room"},
    {0xa1, "No.2 Laboratory"},
    {0xa2, "Dam Control Tower (Bottom)"},
    {0xa3, "B9 Passage A"},
    {0xa4, "B9 Microwave Furnace Room"},
    {0xa5, "Warehouse"},
    {0xa6, "Large Elevator"},
    {0xad, "Storage"},  // the wiki's Service entrance
    {0xae, "B9 Passage B"},
};

// The room's name, or nullptr for an id with no confirmed room.
constexpr const char* nameOf(uint16_t scene) {
    for (const Room& room : kRooms) {
        if (room.scene == scene) return room.name;
    }
    return nullptr;
}

}  // namespace room_names
