#pragma once
#include <cstdint>
#include <vector>

struct VtableTrace {
    uintptr_t address;
    unsigned count;
};

struct Config {
    uint16_t port = 27960;
    bool coop = false;
    bool trace = false;
    bool overlay = true;  // D3D hooks installed; the panel itself stays hidden until F8
    bool netTrace = false;  // record the partner's pad and state packets to coop/net_trace.bin
    std::vector<VtableTrace> traceVtables;
};

// Reads <game dir>\coop\adapter.ini, writing the defaults first when it is missing.
Config loadConfig();
