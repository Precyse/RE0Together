#pragma once
#include "net_client.h"

namespace net_pad {

// Net thread: tracks which peer drives the partner (the first non-local slot).
void onSession(const SessionSnapshot& session);

// Net thread: buffers the frames of a PAD_FRAME packet from the driving peer.
void onPacket(const GameFrame& frame);

// True while a remote peer is present.
bool active();

// Slot of the driving peer, or -1.
int peerSlot();

// Game thread, once per frame: moves the jitter buffer forward one frame.
void advance();

// Game thread: pad object answering with the remote frame; cloned from realPad on first use.
void* object(void* realPad);

// Game thread: analog block of the current remote frame.
void* analog();

}  // namespace net_pad
