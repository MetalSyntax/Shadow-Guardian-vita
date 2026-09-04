#ifndef SHADOW_AUDIO_H
#define SHADOW_AUDIO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void audio_init(void);
void audio_shutdown(void);
int  audio_write_pcm(const void *data, size_t size_bytes);
void audio_reset_buffer(void);

#ifdef __cplusplus
}
#endif

#endif // SHADOW_AUDIO_H
