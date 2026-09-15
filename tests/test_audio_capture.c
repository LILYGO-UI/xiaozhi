#define _GNU_SOURCE
#include <SDL.h>
#include <assert.h>
#include <string.h>

static Uint32 capture_dequeue(SDL_AudioDeviceID device, void *data, Uint32 len);
static Uint32 capture_queued(SDL_AudioDeviceID device);
static Uint32 capture_ticks(void);
static void capture_delay(Uint32 ms);
static SDL_AudioStatus capture_status(SDL_AudioDeviceID device);

// Exercise the production frame reader with deterministic SDL short reads,
// without requiring a microphone or recording audio during CTest.
#define SDL_DequeueAudio capture_dequeue
#define SDL_GetQueuedAudioSize capture_queued
#define SDL_GetTicks capture_ticks
#define SDL_Delay capture_delay
#define SDL_GetAudioDeviceStatus capture_status
#define main xiaozhi_helper_main
#include "../src/protocol/xiaozhi_helper.c"
#undef main

static const Uint32 *chunks;
static size_t chunk_count;
static size_t chunk_index;
static Uint32 source_offset;
static Uint32 ticks;
static SDL_AudioStatus device_status;
static int stop_on_delay;
static int draining;

static Uint32 capture_dequeue(SDL_AudioDeviceID device, void *data, Uint32 len)
{
    (void)device;
    const Uint32 count = chunk_index < chunk_count ? chunks[chunk_index++] : 0;
    assert(count <= len);
    for (Uint32 i = 0; i < count; ++i) ((Uint8 *)data)[i] = (Uint8)(source_offset++ % 251);
    return count;
}

static Uint32 capture_queued(SDL_AudioDeviceID device)
{
    (void)device;
    // CoreAudio can report bytes at 44.1 kHz before conversion to 16 kHz.
    return draining && ticks >= 30 ? 0 : 4096;
}

static Uint32 capture_ticks(void) { return ticks; }
static void capture_delay(Uint32 ms)
{
    ticks += ms;
    if (stop_on_delay) g_stop = 1;
}
static SDL_AudioStatus capture_status(SDL_AudioDeviceID device)
{
    (void)device;
    return device_status;
}

static void reset_capture(const Uint32 *next_chunks, size_t count)
{
    chunks = next_chunks;
    chunk_count = count;
    chunk_index = 0;
    source_offset = 0;
    ticks = 0;
    device_status = SDL_AUDIO_PLAYING;
    stop_on_delay = 0;
    draining = 0;
    g_stop = 0;
    g_abort_requested = 0;
}

int main(void)
{
    pcm_handle_t cap = {0};
    cap.pcm = 1;
    cap.channels = 1;
    int16_t frame[OPUS_FRAME_SAMPLES];

    const Uint32 partial[] = {1482, 0, 438, 0, 700, 600, 0, 620};
    reset_capture(partial, sizeof(partial) / sizeof(partial[0]));
    for (int n = 0; n < 2; ++n) {
        memset(frame, 0xff, sizeof(frame));
        assert(pcm_read_frames(&cap, frame, OPUS_FRAME_SAMPLES) == 0);
        for (size_t i = 0; i < sizeof(frame); ++i)
            assert(((Uint8 *)frame)[i] == (Uint8)((n * sizeof(frame) + i) % 251));
    }
    assert(chunk_index == chunk_count);

    const Uint32 full[] = {sizeof(frame)};
    reset_capture(full, 1);
    assert(pcm_read_frames(&cap, frame, OPUS_FRAME_SAMPLES) == 0);
    assert(ticks == 0);

    const Uint32 stalled[] = {100};
    reset_capture(stalled, 1);
    assert(pcm_read_frames(&cap, frame, OPUS_FRAME_SAMPLES) == -1);
    assert(ticks == 5000);

    reset_capture(NULL, 0);
    ticks = UINT32_MAX - 100;  // SDL's millisecond clock wraps around.
    assert(pcm_read_frames(&cap, frame, OPUS_FRAME_SAMPLES) == -1);
    assert(ticks == 4899);

    reset_capture(NULL, 0);
    device_status = SDL_AUDIO_STOPPED;
    assert(pcm_read_frames(&cap, frame, OPUS_FRAME_SAMPLES) == -1);
    assert(ticks == 0);

    reset_capture(NULL, 0);
    stop_on_delay = 1;
    assert(pcm_read_frames(&cap, frame, OPUS_FRAME_SAMPLES) == -1);
    assert(ticks == 5);

    reset_capture(NULL, 0);
    draining = 1;
    assert(finish_playback(&cap) == 0);
    assert(ticks == 30);  // Completion must wait for the final queued bytes.
    reset_capture(NULL, 0);
    stop_on_delay = 1;
    assert(finish_playback(&cap) == -1);
    assert(ticks == 5);  // Cancellation does not wait for a stuck output device.
    reset_capture(NULL, 0);
    g_abort_requested = 1;
    assert(pcm_read_frames(&cap, frame, OPUS_FRAME_SAMPLES) == -1);
    assert(finish_playback(&cap) == -1);
    assert(ticks == 0);
    return 0;
}
