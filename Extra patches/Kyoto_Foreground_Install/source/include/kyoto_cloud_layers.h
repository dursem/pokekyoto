#ifndef GUARD_KYOTO_CLOUD_LAYERS_H
#define GUARD_KYOTO_CLOUD_LAYERS_H

void KyotoCloud_Create(void);
void KyotoCloud_Destroy(void);
void KyotoCloud_Prepare(void);
void KyotoCloud_VBlank(void);
void KyotoCloud_OnCallbackChange(void (*callback)(void));
void KyotoCloud_SetFieldLoading(void);
void KyotoCloud_BeforePaletteTransfer(void);
#endif
