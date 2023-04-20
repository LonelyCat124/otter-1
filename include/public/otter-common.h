#if !defined(OTTER_COMMON_H)
#define OTTER_COMMON_H

#include <stdint.h>
#include <stdbool.h>

typedef uint64_t unique_id_t;
typedef uint32_t otter_string_ref_t;

typedef struct otter_src_location_t {
    const char *file;
    const char *func;
    int   line;
} otter_src_location_t;

typedef enum {
    otter_event_model_omp,
    otter_event_model_serial,
    otter_event_model_task_graph
} otter_event_model_t;

typedef struct otter_opt_t {
    char                *hostname;
    char                *tracename;
    char                *tracepath;
    char                *archive_name;
    bool                 append_hostname;
    otter_event_model_t  event_model;
} otter_opt_t;

#endif // OTTER_COMMON_H