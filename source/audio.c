#include "audio.h"
#include "utils/logger.h"

#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_SAMPLE_RATE   44100
#define AUDIO_GRAIN_SAMPLES 2048
#define AUDIO_CHANNELS      2
#define AUDIO_BYTES_PER_SAMPLE 2 // 16-bit signed PCM
#define AUDIO_BYTES_PER_FRAME (AUDIO_CHANNELS * AUDIO_BYTES_PER_SAMPLE) // 4 bytes
#define AUDIO_GRAIN_BYTES   (AUDIO_GRAIN_SAMPLES * AUDIO_BYTES_PER_FRAME) // 8192 bytes

#define AUDIO_FIFO_CAPACITY (128 * 1024) // 128 KB ring buffer

static uint8_t g_fifo[AUDIO_FIFO_CAPACITY];
static size_t  g_fifo_head = 0; // write index
static size_t  g_fifo_tail = 0; // read index
static size_t  g_fifo_used = 0;
static pthread_mutex_t g_fifo_mutex = PTHREAD_MUTEX_INITIALIZER;

static int     g_audio_port = -1;
static SceUID  g_audio_thread = -1;
static volatile int g_audio_running = 0;
static int     g_audio_initialized = 0;

static unsigned long long g_pcm_bytes_written = 0;
static unsigned long long g_pcm_frames_played = 0;

static int audio_mixer_thread(SceSize args, void *argp) {
    (void)args;
    (void)argp;

    static int16_t out_buf[2][AUDIO_GRAIN_SAMPLES * AUDIO_CHANNELS];
    int buf_idx = 0;

    l_info("[audio] sceAudioOut mixer thread running on USER_1");

    while (g_audio_running) {
        int16_t *cur_buf = out_buf[buf_idx];
        size_t bytes_needed = AUDIO_GRAIN_BYTES;
        size_t bytes_to_copy = 0;

        pthread_mutex_lock(&g_fifo_mutex);
        if (g_fifo_used > 0) {
            bytes_to_copy = (g_fifo_used < bytes_needed) ? g_fifo_used : bytes_needed;

            // Direct or wrapped copy from ring buffer
            size_t first_chunk = AUDIO_FIFO_CAPACITY - g_fifo_tail;
            if (first_chunk > bytes_to_copy) {
                first_chunk = bytes_to_copy;
            }
            memcpy(cur_buf, &g_fifo[g_fifo_tail], first_chunk);

            size_t second_chunk = bytes_to_copy - first_chunk;
            if (second_chunk > 0) {
                memcpy((uint8_t *)cur_buf + first_chunk, &g_fifo[0], second_chunk);
            }

            g_fifo_tail = (g_fifo_tail + bytes_to_copy) % AUDIO_FIFO_CAPACITY;
            g_fifo_used -= bytes_to_copy;
            g_pcm_frames_played += (bytes_to_copy / AUDIO_BYTES_PER_FRAME);
        }
        pthread_mutex_unlock(&g_fifo_mutex);

        // If FIFO was under-filled, pad remaining samples with silence
        if (bytes_to_copy < bytes_needed) {
            memset((uint8_t *)cur_buf + bytes_to_copy, 0, bytes_needed - bytes_to_copy);
        }

        // Hardware blocking output: acts as the audio clock
        sceAudioOutOutput(g_audio_port, cur_buf);
        buf_idx ^= 1;
    }

    l_info("[audio] sceAudioOut mixer thread exiting");
    return 0;
}

void audio_init(void) {
    if (g_audio_initialized) return;

    memset(g_fifo, 0, sizeof(g_fifo));
    g_fifo_head = 0;
    g_fifo_tail = 0;
    g_fifo_used = 0;
    g_pcm_bytes_written = 0;
    g_pcm_frames_played = 0;

    g_audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,
                                       AUDIO_GRAIN_SAMPLES,
                                       AUDIO_SAMPLE_RATE,
                                       SCE_AUDIO_OUT_MODE_STEREO);
    if (g_audio_port < 0) {
        l_error("[audio] sceAudioOutOpenPort failed (0x%08X)", (unsigned)g_audio_port);
        return;
    }

    g_audio_running = 1;
    g_audio_thread = sceKernelCreateThread("sg_audio_mixer",
                                           audio_mixer_thread,
                                           0x10000100, // Priority
                                           0x10000,    // Stack size (64KB)
                                           0,
                                           SCE_KERNEL_CPU_MASK_USER_1, // Dedicated to Core 1
                                           NULL);
    if (g_audio_thread < 0) {
        l_error("[audio] sceKernelCreateThread failed (0x%08X)", (unsigned)g_audio_thread);
        sceAudioOutReleasePort(g_audio_port);
        g_audio_port = -1;
        g_audio_running = 0;
        return;
    }

    sceKernelStartThread(g_audio_thread, 0, NULL);
    g_audio_initialized = 1;
    l_info("[audio] Native audio subsystem initialized (44100Hz Stereo, grain %d)", AUDIO_GRAIN_SAMPLES);
}

void audio_shutdown(void) {
    if (!g_audio_initialized) return;

    g_audio_running = 0;
    g_audio_initialized = 0;

    if (g_audio_thread >= 0) {
        sceKernelWaitThreadEnd(g_audio_thread, NULL, NULL);
        sceKernelDeleteThread(g_audio_thread);
        g_audio_thread = -1;
    }

    if (g_audio_port >= 0) {
        sceAudioOutReleasePort(g_audio_port);
        g_audio_port = -1;
    }

    l_info("[audio] Native audio subsystem shutdown successfully (frames played: %llu)", g_pcm_frames_played);
}

int audio_write_pcm(const void *data, size_t size_bytes) {
    if (!g_audio_initialized || !data || size_bytes == 0) return 0;

    const uint8_t *src = (const uint8_t *)data;
    size_t remaining = size_bytes;
    int guard = 0;

    while (remaining > 0 && guard < 1000) {
        pthread_mutex_lock(&g_fifo_mutex);
        size_t free_space = AUDIO_FIFO_CAPACITY - g_fifo_used;
        size_t chunk = (remaining < free_space) ? remaining : free_space;

        if (chunk > 0) {
            size_t first_chunk = AUDIO_FIFO_CAPACITY - g_fifo_head;
            if (first_chunk > chunk) {
                first_chunk = chunk;
            }
            memcpy(&g_fifo[g_fifo_head], src, first_chunk);

            size_t second_chunk = chunk - first_chunk;
            if (second_chunk > 0) {
                memcpy(&g_fifo[0], src + first_chunk, second_chunk);
            }

            g_fifo_head = (g_fifo_head + chunk) % AUDIO_FIFO_CAPACITY;
            g_fifo_used += chunk;
            src += chunk;
            remaining -= chunk;
            g_pcm_bytes_written += chunk;
        }
        pthread_mutex_unlock(&g_fifo_mutex);

        if (remaining > 0) {
            // Buffer full, yield briefly to let mixer thread consume data
            sceKernelDelayThread(1000); // 1 ms
            guard++;
        }
    }

    return (int)(size_bytes - remaining);
}

void audio_reset_buffer(void) {
    pthread_mutex_lock(&g_fifo_mutex);
    g_fifo_head = 0;
    g_fifo_tail = 0;
    g_fifo_used = 0;
    pthread_mutex_unlock(&g_fifo_mutex);
}
