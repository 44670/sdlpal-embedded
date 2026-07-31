/*
 * Deterministic host test for the Cardputer extreme music command/state
 * semantics.  This includes the production translation unit so the assertions
 * exercise its real anonymous-namespace reducer rather than a copied model.
 */

#include "../engine_bridge/pal_engine_target_music.cpp"

#include <assert.h>
#include <stdio.h>

namespace
{

CardputerExtremeAudioRenderCallback test_render_callback;
void *test_render_user;
enum class TestProfileMode
{
    Valid,
    WrongCount,
    NonemptyReservedSlot,
    InvalidTrack,
};
TestProfileMode test_profile_mode = TestProfileMode::Valid;
int32_t test_last_source_fault;
uint32_t test_source_faults;
uint32_t test_reserved_track_map_calls;

const uint8_t test_rix[] = {
    0xaa, 0x55, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x0e, 0x00, 0x00, 0x80,
};

void
drain()
{
    music_drain_commands();
}

void
assert_zero_samples(const int16_t *samples, size_t count)
{
    for (size_t i = 0; i < count; i++)
    {
        assert(samples[i] == 0);
    }
}

void
test_startup_profile_validation()
{
    test_profile_mode = TestProfileMode::WrongCount;
    assert(AUDIO_OpenDevice() == -4);
    assert(!gAudioDevice.fOpened);

    test_profile_mode = TestProfileMode::NonemptyReservedSlot;
    assert(AUDIO_OpenDevice() == -4);
    assert(!gAudioDevice.fOpened);

    test_profile_mode = TestProfileMode::InvalidTrack;
    assert(AUDIO_OpenDevice() == -4);
    assert(!gAudioDevice.fOpened);

    test_profile_mode = TestProfileMode::Valid;
    assert(AUDIO_OpenDevice() == 0);
    assert(gAudioDevice.fOpened);
    assert(test_render_callback != nullptr);
}

void
test_muted_play_updates_and_freezes_control_fade()
{
    int16_t samples[kTickSamples];
    uint32_t ticks_before;
    uint32_t fade_phase_before;
    uint32_t fade_remaining_before;
    bool finished = false;

    AUDIO_PlayMusic(1, TRUE, 0.0f);
    drain();
    assert(pal_music_runtime.enabled);
    assert(pal_music_runtime.playing);
    assert(pal_music_runtime.current_track == 1);
    assert(pal_music_runtime.current_loop);
    assert(pal_music_runtime.fade == FadeState::None);

    AUDIO_EnableMusic(FALSE);
    drain();
    assert(!pal_music_runtime.enabled);
    assert(pal_music_runtime.playing);
    assert(pal_music_runtime.current_track == 1);
    assert(pal_music_runtime.pending_track == -1);

    /*
     * Like desktop RIX, this target accepts Play while its mixer is muted.
     * The target deliberately uses deterministic sample-clock freeze: it
     * installs the fade control now but does not advance it until Enable.
     * Desktop RIX has one wall-clock exception: a fade-out that has not yet
     * consumed any samples catches up from SDL_GetTicks() on its first callback.
     */
    AUDIO_PlayMusic(4, TRUE, 1.0f);
    drain();
    assert(!pal_music_runtime.enabled);
    assert(pal_music_runtime.current_track == 1);
    assert(pal_music_runtime.pending_track == 4);
    assert(pal_music_runtime.pending_loop);
    assert(pal_music_runtime.fade == FadeState::Out);

    ticks_before = pal_music_runtime.rendered_ticks;
    fade_phase_before = pal_music_runtime.fade_phase_q31;
    fade_remaining_before = pal_music_runtime.fade_remaining;
    memset(samples, 0x5a, sizeof(samples));
    test_render_callback(
        test_render_user,
        samples,
        kTickSamples);
    assert_zero_samples(samples, kTickSamples);
    assert(pal_music_runtime.rendered_ticks == ticks_before);
    assert(pal_music_runtime.current_track == 1);
    assert(pal_music_runtime.pending_track == 4);
    assert(pal_music_runtime.fade == FadeState::Out);
    assert(pal_music_runtime.fade_phase_q31 == fade_phase_before);
    assert(pal_music_runtime.fade_remaining == fade_remaining_before);

    /*
     * A later zero-fade request retargets the already active fade-out, just
     * like RIX_Play(), instead of switching immediately while muted.
     */
    AUDIO_PlayMusic(2, FALSE, 0.0f);
    drain();
    assert(!pal_music_runtime.enabled);
    assert(pal_music_runtime.current_track == 1);
    assert(pal_music_runtime.pending_track == 2);
    assert(!pal_music_runtime.pending_loop);
    assert(pal_music_runtime.fade == FadeState::Out);
    assert(pal_music_runtime.fade_phase_q31 == fade_phase_before);
    assert(pal_music_runtime.fade_remaining == fade_remaining_before);

    AUDIO_EnableMusic(TRUE);
    drain();
    assert(pal_music_runtime.enabled);
    assert(pal_music_runtime.playing);
    assert(pal_music_runtime.current_track == 1);
    assert(pal_music_runtime.pending_track == 2);
    assert(pal_music_runtime.fade == FadeState::Out);

    while (pal_music_runtime.fade_remaining != 0)
    {
        finished = music_advance_fade_one_sample();
    }
    assert(finished);
    assert(music_start_pending());
    assert(pal_music_runtime.current_track == 2);
    assert(!pal_music_runtime.current_loop);
    assert(pal_music_runtime.fade == FadeState::None);
}

void
test_zero_fade_retarget_preserves_active_fade_out()
{
    const uint32_t elapsed_200ms =
        kSampleRate / 5u;
    bool finished = false;

    AUDIO_PlayMusic(0, FALSE, 1.0f);
    drain();
    assert(pal_music_runtime.current_track == 2);
    assert(pal_music_runtime.pending_track == 0);
    assert(pal_music_runtime.fade == FadeState::Out);

    for (uint32_t i = 0; i < elapsed_200ms; i++)
    {
        assert(!music_advance_fade_one_sample());
    }

    const uint32_t phase_before =
        pal_music_runtime.fade_phase_q31;
    const uint32_t remaining_before =
        pal_music_runtime.fade_remaining;
    const uint32_t total_before =
        pal_music_runtime.fade_total;

    AUDIO_PlayMusic(3, TRUE, 0.0f);
    drain();

    assert(pal_music_runtime.current_track == 2);
    assert(pal_music_runtime.pending_track == 3);
    assert(pal_music_runtime.pending_loop);
    assert(pal_music_runtime.fade == FadeState::Out);
    assert(pal_music_runtime.fade_phase_q31 == phase_before);
    assert(pal_music_runtime.fade_remaining == remaining_before);
    assert(pal_music_runtime.fade_total == total_before);
    assert(pal_music_runtime.fade_in_samples == 0);

    while (pal_music_runtime.fade_remaining != 0)
    {
        finished = music_advance_fade_one_sample();
    }
    assert(finished);
    assert(music_start_pending());
    assert(pal_music_runtime.current_track == 3);
    assert(pal_music_runtime.current_loop);
    assert(pal_music_runtime.fade == FadeState::None);
}

void
test_same_track_restarts_after_pending_fade_out()
{
    const uint32_t elapsed_100ms =
        kSampleRate / 10u;
    uint32_t phase_before;
    uint32_t remaining_before;
    uint32_t total_before;
    bool finished = false;

    AUDIO_PlayMusic(3, TRUE, 0.0f);
    drain();
    assert(pal_music_runtime.current_track == 3);
    assert(pal_music_runtime.pending_track == -1);
    assert(pal_music_runtime.fade == FadeState::None);

    AUDIO_PlayMusic(0, FALSE, 1.0f);
    drain();
    assert(pal_music_runtime.current_track == 3);
    assert(pal_music_runtime.pending_track == 0);
    assert(pal_music_runtime.fade == FadeState::Out);
    for (uint32_t i = 0; i < elapsed_100ms; i++)
    {
        assert(!music_advance_fade_one_sample());
    }
    phase_before = pal_music_runtime.fade_phase_q31;
    remaining_before = pal_music_runtime.fade_remaining;
    total_before = pal_music_runtime.fade_total;

    /*
     * Desktop RIX only uses its same-track fast path when no destination is
     * pending.  Here it must finish fading out, rewind track 3, then fade in.
     */
    AUDIO_PlayMusic(3, FALSE, 0.5f);
    drain();
    assert(pal_music_runtime.current_track == 3);
    assert(pal_music_runtime.current_loop);
    assert(pal_music_runtime.pending_track == 3);
    assert(!pal_music_runtime.pending_loop);
    assert(pal_music_runtime.fade == FadeState::Out);
    assert(pal_music_runtime.fade_phase_q31 == phase_before);
    assert(pal_music_runtime.fade_remaining == remaining_before);
    assert(pal_music_runtime.fade_total == total_before);
    assert(
        pal_music_runtime.fade_in_samples ==
        music_half_fade_samples(0.5f));

    while (pal_music_runtime.fade_remaining != 0)
    {
        finished = music_advance_fade_one_sample();
    }
    assert(finished);
    assert(music_start_pending());
    assert(pal_music_runtime.current_track == 3);
    assert(!pal_music_runtime.current_loop);
    assert(pal_music_runtime.pending_track == -1);
    assert(pal_music_runtime.fade == FadeState::In);
}

void
test_enable_does_not_replay_completed_track()
{
    uint32_t generation;

    AUDIO_PlayMusic(1, FALSE, 0.0f);
    drain();
    assert(pal_music_runtime.playing);
    assert(pal_music_runtime.current_track == 1);
    generation = pal_music_runtime.applied_play_generation;

    /* This is the reducer state reached when a non-looping source ends. */
    music_stop_now();
    assert(!pal_music_runtime.playing);
    assert(pal_music_runtime.applied_play_generation == generation);

    AUDIO_EnableMusic(FALSE);
    AUDIO_EnableMusic(TRUE);
    drain();
    assert(pal_music_runtime.enabled);
    assert(!pal_music_runtime.playing);
    assert(pal_music_runtime.current_track == -1);
    assert(pal_music_runtime.pending_track == -1);
    assert(pal_music_runtime.applied_play_generation == generation);
}

void
test_queue_full_coalesces_latest_full_snapshots()
{
    uint32_t drops_before;
    uint32_t generation;

    AUDIO_EnableMusic(TRUE);
    gConfig.iMusicVolume = PAL_MAX_VOLUME;
    AUDIO_IncreaseVolume();
    AUDIO_PlayMusic(1, TRUE, 0.0f);
    drain();
    generation = pal_music_runtime.applied_play_generation;
    assert(generation != 0);
    assert(xQueueReset(pal_music_command_queue) == pdTRUE);

    /*
     * Fill the queue with unconsumed Play generations.  The final Volume
     * snapshot must coalesce them while retaining the newest track, loop,
     * enable and volume fields together.
     */
    for (unsigned i = 0; i < kCommandCount; i++)
    {
        AUDIO_PlayMusic(
            static_cast<INT>(i + 1u),
            (i & 1u) == 0u ? TRUE : FALSE,
            0.0f);
    }
    assert(pal_music_command_queue->count == kCommandCount);
    assert(pal_music_desired.track == 8);
    assert(!pal_music_desired.loop);
    assert(pal_music_desired.enabled);

    drops_before = pal_music_command_drops;
    gConfig.iMusicVolume = 0;
    AUDIO_DecreaseVolume();
    assert(pal_music_command_queue->count == 1);
    assert(pal_music_command_drops == drops_before + 1u);
    drain();

    assert(pal_music_runtime.enabled);
    assert(pal_music_runtime.volume_q15 == 0);
    assert(pal_music_runtime.current_track == 8);
    assert(!pal_music_runtime.current_loop);
    assert(
        pal_music_runtime.applied_play_generation ==
        pal_music_desired.play_generation);
    assert(pal_music_runtime.applied_play_generation != generation);

    gConfig.iMusicVolume = PAL_MAX_VOLUME;
    AUDIO_IncreaseVolume();
    drain();
    assert(pal_music_runtime.enabled);
    assert(pal_music_runtime.volume_q15 == kQ15One);

    /*
     * Repeat with Enable as the coalescing snapshot.  Its generation is the
     * last queued Play, and its other fields must still carry the latest
     * loop and volume state.
     */
    assert(xQueueReset(pal_music_command_queue) == pdTRUE);
    for (unsigned i = 0; i < kCommandCount; i++)
    {
        AUDIO_PlayMusic(
            static_cast<INT>(kCommandCount - i),
            i + 1u == kCommandCount ? TRUE : FALSE,
            0.0f);
    }
    assert(pal_music_command_queue->count == kCommandCount);
    assert(pal_music_desired.track == 1);
    assert(pal_music_desired.loop);
    assert(pal_music_desired.volume_q15 == kQ15One);

    drops_before = pal_music_command_drops;
    AUDIO_EnableMusic(FALSE);
    assert(pal_music_command_queue->count == 1);
    assert(pal_music_command_drops == drops_before + 1u);
    drain();

    assert(!pal_music_runtime.enabled);
    assert(pal_music_runtime.volume_q15 == kQ15One);
    assert(pal_music_runtime.current_track == 1);
    assert(pal_music_runtime.current_loop);
    assert(
        pal_music_runtime.applied_play_generation ==
        pal_music_desired.play_generation);

    AUDIO_EnableMusic(TRUE);
    drain();
    assert(pal_music_runtime.enabled);
    assert(pal_music_runtime.current_track == 1);
    assert(pal_music_runtime.current_loop);
}

void
test_zero_volume_freezes_playback()
{
    int16_t samples[kTickSamples];
    uint32_t ticks_before;
    uint32_t fade_phase_before;
    uint32_t fade_remaining_before;

    AUDIO_PlayMusic(0, FALSE, 0.0f);
    drain();
    assert(!pal_music_runtime.playing);
    AUDIO_PlayMusic(1, TRUE, 1.0f);
    drain();
    assert(pal_music_runtime.playing);
    assert(pal_music_runtime.current_track == 1);
    assert(pal_music_runtime.fade == FadeState::In);

    gConfig.iMusicVolume = 0;
    AUDIO_DecreaseVolume();
    drain();
    assert(pal_music_runtime.volume_q15 == 0);
    ticks_before = pal_music_runtime.rendered_ticks;
    fade_phase_before = pal_music_runtime.fade_phase_q31;
    fade_remaining_before = pal_music_runtime.fade_remaining;

    memset(samples, 0x5a, sizeof(samples));
    test_render_callback(
        test_render_user,
        samples,
        kTickSamples);
    assert_zero_samples(samples, kTickSamples);
    assert(pal_music_runtime.rendered_ticks == ticks_before);
    assert(pal_music_runtime.current_track == 1);
    assert(pal_music_runtime.fade_phase_q31 == fade_phase_before);
    assert(pal_music_runtime.fade_remaining == fade_remaining_before);

    gConfig.iMusicVolume = PAL_MAX_VOLUME;
    AUDIO_IncreaseVolume();
    drain();
    assert(pal_music_runtime.volume_q15 == kQ15One);
    test_render_callback(
        test_render_user,
        samples,
        kTickSamples);
    assert(
        pal_music_runtime.rendered_ticks > ticks_before ||
        pal_music_runtime.fade_remaining != fade_remaining_before);
}

void
test_reserved_track_29_is_stop()
{
    uint32_t source_faults_before;

    AUDIO_PlayMusic(1, TRUE, 0.0f);
    drain();
    assert(pal_music_runtime.playing);
    assert(pal_music_runtime.current_track == 1);

    source_faults_before = test_source_faults;
    test_reserved_track_map_calls = 0;
    AUDIO_PlayMusic(kEmptyMusTrackB, FALSE, 0.0f);
    drain();

    assert(pal_music_desired.track == 0);
    assert(!pal_music_runtime.playing);
    assert(pal_music_runtime.current_track == -1);
    assert(test_reserved_track_map_calls == 0);
    assert(test_source_faults == source_faults_before);
}

void
test_runtime_source_fault_telemetry()
{
    uint32_t generation;

    test_profile_mode = TestProfileMode::InvalidTrack;
    test_last_source_fault = 0;
    test_source_faults = 0;

    AUDIO_PlayMusic(5, FALSE, 0.0f);
    drain();

    assert(!pal_music_runtime.playing);
    assert(pal_music_runtime.missing_tracks == 1);
    assert(test_source_faults == 1);
    assert(test_last_source_fault == 5);
    generation = pal_music_runtime.applied_play_generation;

    AUDIO_EnableMusic(FALSE);
    AUDIO_EnableMusic(TRUE);
    drain();
    assert(!pal_music_runtime.playing);
    assert(pal_music_runtime.applied_play_generation == generation);
    assert(test_source_faults == 1);
    assert(test_last_source_fault == 5);
    test_profile_mode = TestProfileMode::Valid;
}

} // namespace

extern "C" {

CONFIGURATION gConfig = {};

QueueHandle_t
xQueueCreateStatic(
    UBaseType_t length,
    UBaseType_t item_size,
    uint8_t *storage,
    StaticQueue_t *queue)
{
    assert(length > 0);
    assert(item_size > 0);
    assert(storage != nullptr);
    assert(queue != nullptr);
    queue->storage = storage;
    queue->length = length;
    queue->item_size = item_size;
    queue->head = 0;
    queue->count = 0;
    return queue;
}

BaseType_t
xQueueSend(
    QueueHandle_t queue,
    const void *item,
    TickType_t ticks_to_wait)
{
    UBaseType_t tail;

    (void)ticks_to_wait;
    if (queue == nullptr ||
        item == nullptr ||
        queue->count == queue->length)
    {
        return pdFALSE;
    }
    tail = (queue->head + queue->count) % queue->length;
    memcpy(
        queue->storage + tail * queue->item_size,
        item,
        queue->item_size);
    queue->count++;
    return pdTRUE;
}

BaseType_t
xQueueReceive(
    QueueHandle_t queue,
    void *item,
    TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
    if (queue == nullptr ||
        item == nullptr ||
        queue->count == 0)
    {
        return pdFALSE;
    }
    memcpy(
        item,
        queue->storage + queue->head * queue->item_size,
        queue->item_size);
    queue->head = (queue->head + 1u) % queue->length;
    queue->count--;
    return pdTRUE;
}

BaseType_t
xQueueReset(QueueHandle_t queue)
{
    if (queue == nullptr)
    {
        return pdFALSE;
    }
    queue->head = 0;
    queue->count = 0;
    return pdTRUE;
}

bool
PalContract_TargetOpenNorPack(PalPack *pack)
{
    memset(pack, 0, sizeof(*pack));
    return true;
}

bool
PalMusic_MapMus(
    const PalPack *nor_pack,
    uint16_t track_num,
    PalMusicTrack *track)
{
    (void)nor_pack;
    if (track_num == 0 || track == nullptr)
    {
        return false;
    }
    if (track_num == kEmptyMusTrackB)
    {
        test_reserved_track_map_calls++;
    }
    if (test_profile_mode == TestProfileMode::InvalidTrack &&
        track_num == 5)
    {
        return false;
    }
    track->data = test_rix;
    track->size = sizeof(test_rix);
    track->track_num = track_num;
    track->format = PAL_MUSIC_FORMAT_RIX;
    return true;
}

bool
PalPack_GetChunkCount(
    const PalPack *pack,
    uint16_t archive_id,
    uint16_t *chunk_count)
{
    (void)pack;
    assert(archive_id == PAL_PACK_ARCHIVE_MUS);
    assert(chunk_count != nullptr);
    *chunk_count =
        test_profile_mode == TestProfileMode::WrongCount
            ? kMusChunkCount - 1u
            : kMusChunkCount;
    return true;
}

bool
PalPack_MapConst(
    const PalPack *pack,
    uint16_t archive_id,
    uint16_t chunk_id,
    PalPackSpan *span)
{
    (void)pack;
    assert(archive_id == PAL_PACK_ARCHIVE_MUS);
    assert(span != nullptr);
    span->data = test_rix;
    span->size =
        test_profile_mode == TestProfileMode::NonemptyReservedSlot &&
                chunk_id == kEmptyMusTrackA
            ? sizeof(test_rix)
            : 0;
    span->format = PAL_PACK_FORMAT_NATIVE;
    span->flags = 0;
    return chunk_id < kMusChunkCount;
}

void PalMameOpl2_Init(void) {}
void PalMameOpl2_Reset(void) {}
void PalMameOpl2_Write(uint8_t reg, uint8_t value)
{
    (void)reg;
    (void)value;
}
void PalMameOpl2_Render(int16_t *samples, size_t frames)
{
    memset(samples, 0, frames * sizeof(*samples));
}
size_t PalMameOpl2_StateBytes(void)
{
    return 0;
}
size_t PalMameOpl2_TableBytes(void)
{
    return 0;
}

bool
CardputerExtremeAudio_Begin(
    CardputerExtremeAudioRenderCallback render,
    void *user)
{
    test_render_callback = render;
    test_render_user = user;
    return true;
}

bool CardputerExtremeAudio_SetPaused(bool paused)
{
    (void)paused;
    return true;
}

bool CardputerExtremeAudio_Stop(void)
{
    return true;
}

bool CardputerExtremeAudio_Started(void)
{
    return true;
}

void
CardputerExtremeAudio_GetTelemetry(
    CardputerExtremeAudioTelemetry *telemetry)
{
    memset(telemetry, 0, sizeof(*telemetry));
}

void
CardputerExtremeAudio_RecordSourceFault(
    int32_t source_code)
{
    test_source_faults++;
    test_last_source_fault = source_code;
}

void CardputerExtremeAudio_PollTelemetry(void) {}
void CardputerExtremeAudio_LogTelemetry(const char *stage)
{
    (void)stage;
}

} // extern "C"

int
main()
{
    gConfig.iMusicVolume = PAL_MAX_VOLUME;

    test_startup_profile_validation();
    test_muted_play_updates_and_freezes_control_fade();
    test_zero_fade_retarget_preserves_active_fade_out();
    test_same_track_restarts_after_pending_fade_out();
    test_enable_does_not_replay_completed_track();
    test_queue_full_coalesces_latest_full_snapshots();
    test_zero_volume_freezes_playback();
    test_reserved_track_29_is_stop();
    test_runtime_source_fault_telemetry();

    AUDIO_CloseDevice();
    puts("pal_engine_target_music_state_test: PASS");
    return 0;
}
