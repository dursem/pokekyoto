#ifndef GUARD_KYOTO_BATTLE_SLIDE_H
#define GUARD_KYOTO_BATTLE_SLIDE_H

#include "global.h"

#define KYOTO_INTRO_SLIDE_FRAMES 105
struct Sprite;

// Opal's cubic ease-in/out, shared by sprites and their entry platforms.
static inline s16 KyotoIntroSlideOffset(s32 start, u32 frame)
{
    const u32 duration = KYOTO_INTRO_SLIDE_FRAMES;
    const u32 denominator = duration * duration * duration;
    u32 remaining;
    u32 distance;
    if (frame >= KYOTO_INTRO_SLIDE_FRAMES)
        return 0;
    if (frame * 2 < duration)
        remaining = denominator - 4 * frame * frame * frame;
    else
    {
        frame = duration - frame;
        remaining = 4 * frame * frame * frame;
    }
    distance = ((start < 0 ? -start : start) * remaining + denominator - 1) / denominator;
    return start < 0 ? -distance : distance;
}

void SpriteCB_KyotoTrainerIntroSlide(struct Sprite *sprite);

#endif
