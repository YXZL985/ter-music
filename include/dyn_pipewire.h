#ifndef DYN_PIPEWIRE_H
#define DYN_PIPEWIRE_H

#include <stdint.h>
#include <stddef.h>

/* ---- opaque type forward declarations ---- */
struct pw_main_loop;
struct pw_thread_loop;
struct pw_context;
struct pw_core;
struct pw_stream;
struct pw_properties;
struct pw_buffer;
struct pw_stream_events;
struct spa_pod;
struct spa_pod_builder;
struct spa_audio_info_raw;

/* ---- spa_hook (used as a non-opaque field in audio.c) ---- */
struct spa_list {
    struct spa_list *prev;
    struct spa_list *next;
};

struct spa_hook {
    struct spa_list link;
    void *priv;
    void *cb;
    void *removed;
};

/* ---- SPA pod types ---- */
#define SPA_TYPE_None        0
#define SPA_TYPE_Bool        1
#define SPA_TYPE_Id          2
#define SPA_TYPE_Int         3
#define SPA_TYPE_Long        4
#define SPA_TYPE_Float       5
#define SPA_TYPE_Double      6
#define SPA_TYPE_String      7
#define SPA_TYPE_Bytes       8
#define SPA_TYPE_Rectangle   9
#define SPA_TYPE_Fraction    10
#define SPA_TYPE_Array       11
#define SPA_TYPE_Pointer     12
#define SPA_TYPE_Fd          13
#define SPA_TYPE_Choice      14
#define SPA_TYPE_Object      15
#define SPA_TYPE_Struct      16

/* ---- SPA object types (for spa_pod_object_body.body_type) ---- */
#define SPA_TYPE_OBJECT_Format          0
#define SPA_TYPE_OBJECT_ParamBuffers    1
#define SPA_TYPE_OBJECT_ParamMeta       2
#define SPA_TYPE_OBJECT_ParamIO         3
#define SPA_TYPE_OBJECT_ParamAvailability  4
#define SPA_TYPE_OBJECT_ParamEnumFormat 5
#define SPA_TYPE_OBJECT_ParamProfile    6
#define SPA_TYPE_OBJECT_ParamPortConfig 7

/* ---- SPA param types ---- */
#define SPA_PARAM_Invalid          0
#define SPA_PARAM_PropInfo         1
#define SPA_PARAM_Props            2
#define SPA_PARAM_EnumFormat       3
#define SPA_PARAM_Format           4
#define SPA_PARAM_Buffers          5
#define SPA_PARAM_Meta             6
#define SPA_PARAM_IO               7
#define SPA_PARAM_EnumProfile      8
#define SPA_PARAM_Profile          9
#define SPA_PARAM_EnumPortConfig   10
#define SPA_PARAM_PortConfig       11

/* ---- SPA format property keys ---- */
#define SPA_FORMAT_mediaType        0
#define SPA_FORMAT_mediaSubtype     1
#define SPA_FORMAT_AUDIO_format     2
#define SPA_FORMAT_AUDIO_rate       3
#define SPA_FORMAT_AUDIO_channels   4
#define SPA_FORMAT_AUDIO_position   5

/* ---- SPA media types / subtypes ---- */
#define SPA_MEDIA_TYPE_audio        1
#define SPA_MEDIA_SUBTYPE_raw       1

/* ---- SPA audio formats ---- */
#define SPA_AUDIO_FORMAT_UNKNOWN    0
#define SPA_AUDIO_FORMAT_ENCODED    1
#define SPA_AUDIO_FORMAT_U8         2
#define SPA_AUDIO_FORMAT_S16        3
#define SPA_AUDIO_FORMAT_S24        4
#define SPA_AUDIO_FORMAT_S32        5
#define SPA_AUDIO_FORMAT_F32        6
#define SPA_AUDIO_FORMAT_F64        7
#define SPA_AUDIO_FORMAT_U8P        8
#define SPA_AUDIO_FORMAT_S16P       9
#define SPA_AUDIO_FORMAT_S24P      10
#define SPA_AUDIO_FORMAT_S32P      11
#define SPA_AUDIO_FORMAT_F32P      12
#define SPA_AUDIO_FORMAT_F64P      13
#define SPA_AUDIO_FORMAT_S24_32    14
#define SPA_AUDIO_FORMAT_S24_32P   15
#define SPA_AUDIO_FORMAT_MAX       16

/* ---- PipeWire stream flags ---- */
#define PW_STREAM_FLAG_NONE             0
#define PW_STREAM_FLAG_AUTOCONNECT      (1 << 0)
#define PW_STREAM_FLAG_MAP_BUFFERS      (1 << 3)
#define PW_STREAM_FLAG_RT_PROCESS       (1 << 4)
#define PW_STREAM_FLAG_DONT_RECONNECT   (1 << 6)
#define PW_STREAM_FLAG_ASYNC            (1 << 9)

/* ---- PipeWire direction ---- */
#define PW_DIRECTION_OUTPUT  1

/* ---- PipeWire stream state ---- */
enum pw_stream_state {
    PW_STREAM_STATE_ERROR = -1,
    PW_STREAM_STATE_UNCONNECTED = 0,
    PW_STREAM_STATE_CONNECTING = 1,
    PW_STREAM_STATE_PAUSED = 2,
    PW_STREAM_STATE_STREAMING = 3,
};

/* ---- PipeWire controls ---- */
#define PW_STREAM_CONTROL_VOLUME  0
#define PW_STREAM_CONTROL_MUTE    1

/* ---- PipeWire pod builder property flags ---- */
#define SPA_POD_BUILDER_FLAG_BODY       (1 << 0)
#define SPA_POD_BUILDER_FLAG_PROPERTY   (1 << 1)
#define SPA_POD_BUILDER_END             ((uint32_t)0)

/* ---- PipeWire version string for properties ---- */
#define PW_KEY_CONFIG_NAME      "config.name"
#define PW_KEY_NODE_NAME        "node.name"
#define PW_KEY_NODE_NICK        "node.nick"
#define PW_KEY_MEDIA_TYPE       "media.type"
#define PW_KEY_MEDIA_CATEGORY   "media.category"
#define PW_KEY_MEDIA_ROLE       "media.role"
#define PW_KEY_NODE_GROUP       "node.group"
#define PW_KEY_PRIORITY_SESSION "priority.session"
#define PW_KEY_APP_NAME         "application.name"
#define PW_KEY_APP_ID           "application.id"
#define PW_KEY_APP_ICON         "application.icon"
#define PW_KEY_APP_LANGUAGE     "application.language"

/* ---- PipeWire buffer / chunk struct (ABI-compatible subset) ---- */
struct spa_chunk {
    uint32_t offset;
    uint32_t size;
    uint32_t stride;
    int32_t  dummy;
};

/* spa_buffer: followed in memory by metas[] (n_metas * sizeof(spa_meta))
   then datas[] (n_datas * sizeof(spa_data)). We provide a macro to safely
   index into the datas array using known offsets. */
struct spa_buffer {
    uint32_t n_metas;
    uint32_t n_datas;
};

#define SPA_BUFFER_DATA(buf, i) \
    ((struct spa_data *)((uint8_t *)(buf) + sizeof(struct spa_buffer) + \
      (size_t)(buf)->n_metas * sizeof(struct spa_meta)) + (i))

struct spa_meta {
    uint32_t type;
    uint32_t size;
    /* data follows */
};

struct spa_data {
    uint32_t type;
    uint32_t flags;
    int      fd;
    uint32_t mapoffset;
    uint32_t maxsize;
    void    *data;
    struct spa_chunk *chunk;
    int32_t  fdflags;
    uint64_t padding[8];
};

struct pw_buffer {
    struct spa_buffer *buffer;
    void   *user_data;
    uint32_t requested;
    int32_t  dummy;
};

/* ---- PipeWire stream events callbacks ---- */
struct pw_stream_events {
    uint32_t version;
    void (*destroy)(void *data);
    void (*state_changed)(void *data, enum pw_stream_state old,
                          enum pw_stream_state state, const char *error);
    void (*param_changed)(void *data, uint32_t id, const struct spa_pod *param);
    void (*process)(void *data);
    void (*drained)(void *data);
    void (*control_info)(void *data, uint32_t id,
                         const struct spa_pod *param,
                         const struct spa_pod *control);
    void (*io_changed)(void *data, uint32_t id, void *area, uint32_t size);
};

/* ---- function pointer table ---- */
struct pipewire_funcs {
    /* init */
    void    (*init)(int *, char ***);
    void    (*deinit)(void);

    /* main loop */
    struct pw_main_loop *   (*main_loop_new)(const struct pw_properties *);
    void                    (*main_loop_destroy)(struct pw_main_loop *);
    void                    (*main_loop_run)(struct pw_main_loop *);
    void                    (*main_loop_quit)(struct pw_main_loop *);

    /* thread loop */
    struct pw_thread_loop * (*thread_loop_new)(const char *, const struct pw_properties *);
    void                    (*thread_loop_destroy)(struct pw_thread_loop *);
    int                     (*thread_loop_start)(struct pw_thread_loop *);
    void                    (*thread_loop_stop)(struct pw_thread_loop *);
    void                    (*thread_loop_lock)(struct pw_thread_loop *);
    void                    (*thread_loop_unlock)(struct pw_thread_loop *);
    void                    (*thread_loop_signal)(struct pw_thread_loop *, int);
    struct pw_main_loop *   (*thread_loop_get_loop)(struct pw_thread_loop *);

    /* context */
    struct pw_context *     (*context_new)(struct pw_main_loop *,
                              struct pw_properties *, size_t);
    void                    (*context_destroy)(struct pw_context *);
    struct pw_core *        (*context_connect)(struct pw_context *,
                              struct pw_properties *, size_t);

    /* core */
    void (*core_disconnect)(struct pw_core *);

    /* properties */
    struct pw_properties *  (*properties_new)(const char *, ...);
    void                    (*properties_free)(struct pw_properties *);
    int                     (*properties_set)(struct pw_properties *,
                              const char *, const char *);
    const char *            (*properties_get)(struct pw_properties *,
                              const char *);

    /* stream */
    struct pw_stream *      (*stream_new)(struct pw_core *, const char *,
                              struct pw_properties *);
    struct pw_stream *      (*stream_new_simple)(struct pw_main_loop *,
                              const char *, struct pw_properties *);
    void                    (*stream_destroy)(struct pw_stream *);
    int                     (*stream_connect)(struct pw_stream *,
                              int, uint32_t, uint32_t,
                              const struct spa_pod **, uint32_t);
    int                     (*stream_disconnect)(struct pw_stream *);
    int                     (*stream_dequeue_buffer)(struct pw_stream *,
                              struct pw_buffer **);
    int                     (*stream_queue_buffer)(struct pw_stream *,
                              struct pw_buffer *);
    int                     (*stream_set_active)(struct pw_stream *, int);
    int                     (*stream_set_control)(struct pw_stream *,
                              uint32_t, float, ...);
    int                     (*stream_flush)(struct pw_stream *, int);
    enum pw_stream_state    (*stream_get_state)(struct pw_stream *,
                              const char **);
    int                     (*stream_add_listener)(struct pw_stream *,
                              struct spa_hook *,
                              const struct pw_stream_events *, void *);
    void                    (*stream_set_error)(struct pw_stream *,
                              int, const char *, ...);
    int                     (*stream_update_params)(struct pw_stream *,
                              const struct spa_pod **, uint32_t);

    /* state */
    int   loaded;
    void *handle;
};

/* ---- public API ---- */
extern struct pipewire_funcs PW;

int  pipewire_load(void);
void pipewire_unload(void);

/* ---- SPA pod builder helper (builds audio format pod without libspa) ---- */
/* Returns the number of bytes written into dst (max 256). */
int build_audio_format_pod(void *dst, int sample_rate, int channels);

#endif /* DYN_PIPEWIRE_H */
