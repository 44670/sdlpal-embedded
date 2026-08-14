#ifndef PAL_NDS_DBOPL2_H
#define PAL_NDS_DBOPL2_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void NdsDbOpl2_Init(void);
void NdsDbOpl2_Reset(void);
void NdsDbOpl2_Write(uint8_t reg, uint8_t value);
/* Each 16.384kHz input frame writes two identical 32.768kHz PCM samples. */
void NdsDbOpl2_Render(int16_t *samples, size_t frames, uint8_t volume);

#ifdef __cplusplus
}
#endif

#endif
