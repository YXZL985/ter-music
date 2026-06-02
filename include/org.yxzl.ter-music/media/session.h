#ifndef MEDIA_SESSION_H
#define MEDIA_SESSION_H

#include <stdint.h>

void media_session_init(void);
void media_session_tick(void);
void media_session_shutdown(void);
void media_session_notify_seek(uint64_t position_ms);

#endif
