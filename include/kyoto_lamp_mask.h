#ifndef GUARD_KYOTO_LAMP_MASK_H
#define GUARD_KYOTO_LAMP_MASK_H

#include "sprite.h"

// Snapshot final BG state once, immediately before preparing this frame's lamps.
void KyotoLamp_BeginFrame(void);
void KyotoLamp_Prepare(const struct Sprite *sprite, u32 index);
void KyotoLamp_Finish(u32 count);
void KyotoLamp_VBlank(void);

#endif
