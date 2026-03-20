#include "../include/progress.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

// 全局进度跟踪器实例
static ProgressTracker g_tracker = {
    .total_samples_played = 0,
    .sample_rate = 0,
    .play_start_time_us = 0,
    .pause_accumulated_us = 0,
    .last_pause_time_us = 0,
    .is_playing = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER
};

/**
 * 获取当前时间的微秒数
 */
static uint64_t get_current_time_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

void progress_tracker_init(int sample_rate) {
    pthread_mutex_lock(&g_tracker.lock);
    
    g_tracker.sample_rate = sample_rate;
    g_tracker.total_samples_played = 0;
    g_tracker.play_start_time_us = get_current_time_us();
    g_tracker.pause_accumulated_us = 0;
    g_tracker.last_pause_time_us = 0;
    g_tracker.is_playing = 1;
    
    pthread_mutex_unlock(&g_tracker.lock);
}

void progress_tracker_destroy(void) {
    pthread_mutex_destroy(&g_tracker.lock);
}

void progress_tracker_add_samples(int num_samples) {
    pthread_mutex_lock(&g_tracker.lock);
    
    if (g_tracker.is_playing && num_samples > 0) {
        g_tracker.total_samples_played += num_samples;
    }
    
    pthread_mutex_unlock(&g_tracker.lock);
}

int progress_tracker_get_position_seconds(void) {
    pthread_mutex_lock(&g_tracker.lock);
    
    int position;
    
    if (g_tracker.sample_rate > 0) {
        // 基于样本数计算位置（主要方法）
        position = (int)(g_tracker.total_samples_played / g_tracker.sample_rate);
        
        // 如果正在播放，可以用 wall-clock time 进行验证
        if (g_tracker.is_playing) {
            uint64_t current_time = get_current_time_us();
            uint64_t elapsed = current_time - g_tracker.play_start_time_us;
            elapsed -= g_tracker.pause_accumulated_us;
            int wallclock_pos = (int)(elapsed / 1000000);
            
            // 如果两者差异过大（>5 秒），可用于调试或校正
            // 当前禁用日志输出以避免干扰 ncurses UI
            // int diff = abs(position - wallclock_pos);
            // if (diff > 5) {
            //     fprintf(stderr, "Progress warning: samples=%d, wallclock=%d, diff=%d\n", 
            //             position, wallclock_pos, diff);
            //     // 可以选择使用 wallclock 位置进行校正
            //     // position = wallclock_pos;
            // }
        }
    } else {
        position = 0;
    }
    
    pthread_mutex_unlock(&g_tracker.lock);
    return position;
}

void progress_tracker_seek(int position_seconds) {
    pthread_mutex_lock(&g_tracker.lock);
    
    if (position_seconds < 0) {
        position_seconds = 0;
    }
    
    // 直接设置样本数偏移
    g_tracker.total_samples_played = (int64_t)position_seconds * g_tracker.sample_rate;
    
    // 重置时间基准，确保 wall-clock 跟踪准确
    g_tracker.play_start_time_us = get_current_time_us();
    g_tracker.pause_accumulated_us = 0;
    
    pthread_mutex_unlock(&g_tracker.lock);
}

void progress_tracker_on_pause(void) {
    pthread_mutex_lock(&g_tracker.lock);
    
    g_tracker.last_pause_time_us = get_current_time_us();
    g_tracker.is_playing = 0;
    
    pthread_mutex_unlock(&g_tracker.lock);
}

void progress_tracker_on_resume(void) {
    pthread_mutex_lock(&g_tracker.lock);
    
    uint64_t now = get_current_time_us();
    uint64_t pause_duration = now - g_tracker.last_pause_time_us;
    
    g_tracker.pause_accumulated_us += pause_duration;
    g_tracker.is_playing = 1;
    
    pthread_mutex_unlock(&g_tracker.lock);
}

void progress_tracker_on_stop(void) {
    pthread_mutex_lock(&g_tracker.lock);
    
    g_tracker.total_samples_played = 0;
    g_tracker.sample_rate = 0;
    g_tracker.play_start_time_us = 0;
    g_tracker.pause_accumulated_us = 0;
    g_tracker.last_pause_time_us = 0;
    g_tracker.is_playing = 0;
    
    pthread_mutex_unlock(&g_tracker.lock);
}

int progress_tracker_get_percent(int total_duration) {
    if (total_duration <= 0) {
        return 0;
    }
    
    int position = progress_tracker_get_position_seconds();
    int percent = (position * 100) / total_duration;
    
    if (percent > 100) {
        percent = 100;
    }
    
    return percent;
}
