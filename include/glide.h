#ifndef GUARD_GLIDE_H
#define GUARD_GLIDE_H
#include "global.h"
#include "constants/event_object_movement.h"
bool8 Glide_IsActive(void);
bool8 Glide_CanUse(void);
void Glide_Toggle(void);
void Glide_Update(void);
void Glide_Reset(void);
// Returns FALSE to preserve the native collision result.
bool8 Glide_GetCollision(struct ObjectEvent *object, s16 x, s16 y, enum Collision *result);
#endif
