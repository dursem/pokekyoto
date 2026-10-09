#ifndef GUARD_KYOTO_LAMP_MASK_H
#define GUARD_KYOTO_LAMP_MASK_H

#include "sprite.h"

void KyotoLamp_Prepare(const struct Sprite *sprite, u32 index);
void KyotoLamp_Finish(u32 count);
void KyotoLamp_VBlank(void);

#endif
