#if !defined(OTTER_TRACE_REGION_DEF_H)
#define OTTER_TRACE_REGION_DEF_H

#include <otf2/OTF2_AttributeList.h>

#include "public/types/stack.h"
#include "public/types/queue.h"
#include "public/otter-common.h"
#include "public/otter-trace/trace-types.h"
#include "public/otter-trace/trace-region-attr.h"
#include "src/otter-trace/trace-region-types.h"

typedef struct trace_region_def_t trace_region_def_t;

// Constructors

trace_region_def_t *
trace_new_master_region(
    trace_location_def_t *loc,
    unique_id_t           encountering_task_id
);

trace_region_def_t *
trace_new_parallel_region(
    unique_id_t    id, 
    unique_id_t    master,
    unique_id_t    encountering_task_id,
    int            flags,
    unsigned int   requested_parallelism
);

trace_region_def_t *
trace_new_phase_region(
    trace_location_def_t *loc,
    otter_phase_region_t  type,
    unique_id_t           encountering_task_id,
    const char           *phase_name
);

trace_region_def_t *
trace_new_sync_region(
    trace_location_def_t *loc,
    otter_sync_region_t   stype,
    trace_task_sync_t     task_sync_mode,
    unique_id_t           encountering_task_id
);

trace_region_def_t *
trace_new_task_region(
    trace_location_def_t *loc,
    trace_region_def_t   *parent_task_region,
    unique_id_t           task_id,
    otter_task_flag_t     flags,
    int                   has_dependences,
    otter_src_location_t *src_location,
    const void           *task_create_ra
);

trace_region_def_t *
trace_new_workshare_region(
    trace_location_def_t *loc,
    otter_work_t          wstype,
    uint64_t              count,
    unique_id_t           encountering_task_id
);


// Destructors

void trace_destroy_master_region(trace_region_def_t *rgn);
void trace_destroy_parallel_region(trace_region_def_t *rgn);
void trace_destroy_phase_region(trace_region_def_t *rgn);
void trace_destroy_sync_region(trace_region_def_t *rgn);
void trace_destroy_task_region(trace_region_def_t *rgn);
void trace_destroy_workshare_region(trace_region_def_t *rgn);


// Add attributes

void trace_add_region_type_attributes(trace_region_def_t *rgn);
void trace_add_master_attributes(trace_region_def_t *rgn);
void trace_add_parallel_attributes(trace_region_def_t *rgn);
void trace_add_phase_attributes(trace_region_def_t *rgn);
void trace_add_sync_attributes(trace_region_def_t *rgn);
void trace_add_task_attributes(trace_region_def_t *rgn);
void trace_add_workshare_attributes(trace_region_def_t *rgn);


// Getters

OTF2_AttributeList *trace_region_get_attribute_list(trace_region_def_t *region);
OTF2_RegionRef trace_region_get_ref(trace_region_def_t *region);
unique_id_t trace_region_get_encountering_task_id(trace_region_def_t *region);
trace_region_type_t trace_region_get_type(trace_region_def_t *region);
trace_region_attr_t trace_region_get_attributes(trace_region_def_t *region);
otter_queue_t *trace_region_get_rgn_def_queue(trace_region_def_t *region);
otter_stack_t *trace_region_get_task_rgn_stack(trace_region_def_t *region);
unsigned int trace_region_get_shared_ref_count(trace_region_def_t *region);


// Setters

void trace_region_set_task_status(trace_region_def_t *region, otter_task_status_t status);


// Lock and unlock shared regions

bool trace_region_is_type(trace_region_def_t *region, trace_region_type_t region_type);
bool trace_region_is_shared(trace_region_def_t *region);
void trace_region_lock(trace_region_def_t *region);
void trace_region_unlock(trace_region_def_t *region);
void trace_region_inc_ref_count(trace_region_def_t *region);
void trace_region_dec_ref_count(trace_region_def_t *region);

// Write region definition to a trace

void trace_region_write_definition_impl(OTF2_GlobalDefWriter *writer, trace_region_def_t *region);


#endif // OTTER_TRACE_REGION_DEF_IMPL_H
