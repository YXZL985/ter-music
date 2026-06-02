#ifndef AUDIO_H
#define AUDIO_H

#include "types.h"

/* ── Extern globals ── */
extern PlayState g_play_state;
extern int g_current_play_index;
extern LoopMode g_loop_mode;
extern pthread_t g_play_thread;
extern int g_play_thread_running;
extern char g_default_audio_device[128];
extern int g_active_backend;
extern float g_playback_speed;
extern int g_audio_sample_rate;
extern int g_audio_bit_rate;
extern int g_audio_bit_depth;
extern char g_audio_codec_name[32];

/* ── Function prototypes ── */
void toggle_playback_speed(void);
void init_ffmpeg();
void init_audio_device();
int audio_backend_is_available(int backend);
void play_audio(int index);
void pause_audio();
void resume_audio();
void stop_audio();
void wait_for_playback_thread_shutdown(void);
void prev_track();
void next_track();
void toggle_loop_mode();
const char *get_loop_mode_str();
void cleanup();
void seek_audio(double position);
int get_and_clear_initial_seek_position(void);
int get_volume_percent(void);
void set_volume_percent(int volume);
void adjust_volume(int delta);
void persist_playback_session_state(void);

#endif
