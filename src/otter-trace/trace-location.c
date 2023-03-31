/**
 * @file trace-location.c
 * @author Adam Tuft
 * @brief Defines trace_location_def_t which represents an OTF2 location, used
 * to record the location's definition in the trace. Responsible for new/delete,
 * adding a thread's attributes to its OTF2 attribute list when recording an 
 * event, and writing a location's definition to the trace.
 */

#define _GNU_SOURCE

#include <stdlib.h>
#include <pthread.h>
#include <sched.h>
#include <otf2/otf2.h>
#include "public/debug.h"
#include "public/otter-trace/trace-location.h"
#include "public/types/queue.h"
#include "public/types/stack.h"
#include "otter-trace/trace-lookup-macros.h"
#include "otter-trace/trace-attributes.h"
#include "otter-trace/trace-archive.h"
#include "otter-trace/trace-unique-refs.h"
#include "otter-trace/trace-check-error-code.h"
#include "otter-trace/trace-static-constants.h"

typedef struct trace_location_def_t {
    unique_id_t             id;
    otter_thread_t          thread_type;
    uint64_t                events;
    otter_stack_t          *rgn_stack;
    otter_queue_t          *rgn_defs;
    otter_stack_t          *rgn_defs_stack;
    OTF2_LocationRef        ref;
    OTF2_LocationType       type;
    OTF2_LocationGroupRef   location_group;
    OTF2_AttributeList     *attributes;
    OTF2_EvtWriter         *evt_writer;
    OTF2_DefWriter         *def_writer;
} trace_location_def_t;

/* Defined in trace-archive.c */
extern OTF2_StringRef attr_name_ref[n_attr_defined][2];
extern OTF2_StringRef attr_label_ref[n_attr_label_defined];

trace_location_def_t *
trace_new_location_definition(
    unique_id_t            id,
    otter_thread_t         thread_type,
    OTF2_LocationType      loc_type,
    OTF2_LocationGroupRef  loc_grp)
{
    trace_location_def_t *new = malloc(sizeof(*new));

    *new = (trace_location_def_t) {
        .id             = id,
        .thread_type    = thread_type,
        .events         = 0,
        .ref            = get_unique_loc_ref(),
        .type           = loc_type,
        .location_group = loc_grp,
        .rgn_stack      = stack_create(),
        .rgn_defs       = queue_create(),
        .rgn_defs_stack = stack_create(),
        .attributes     = OTF2_AttributeList_New()
    };

    OTF2_Archive *Archive = get_global_archive();

    new->evt_writer = OTF2_Archive_GetEvtWriter(Archive, new->ref);
    new->def_writer = OTF2_Archive_GetDefWriter(Archive, new->ref);

    /* Thread location definition is written at thread-end (once all events
       counted) */

    LOG_DEBUG("[t=%lu] location created", id);
    LOG_DEBUG("[t=%lu] %-18s %p", id, "rgn_stack:",      new->rgn_stack);
    LOG_DEBUG("[t=%lu] %-18s %p", id, "rgn_defs:",       new->rgn_defs);
    LOG_DEBUG("[t=%lu] %-18s %p", id, "rgn_defs_stack:", new->rgn_defs_stack);

    return new;
}

void 
trace_destroy_location(trace_location_def_t *loc)
{
    if (loc == NULL) return;
    trace_write_location_definition(loc);
    LOG_DEBUG("[t=%lu] destroying rgn_stack %p", loc->id, loc->rgn_stack);
    stack_destroy(loc->rgn_stack, false, NULL);
    if (loc->rgn_defs)
    {
        LOG_DEBUG("[t=%lu] destroying rgn_defs %p", loc->id, loc->rgn_defs);
        queue_destroy(loc->rgn_defs, false, NULL);
    }
    LOG_DEBUG("[t=%lu] destroying rgn_defs_stack %p", loc->id, loc->rgn_defs_stack);
    stack_destroy(loc->rgn_defs_stack, false, NULL);
    OTF2_AttributeList_Delete(loc->attributes);
    LOG_DEBUG("[t=%lu] destroying location", loc->id);
    free(loc);
    return;
}

void
trace_add_thread_attributes(trace_location_def_t *self)
{
    OTF2_ErrorCode r = OTF2_SUCCESS;
    r = OTF2_AttributeList_AddInt32(self->attributes, attr_cpu, sched_getcpu());
    CHECK_OTF2_ERROR_CODE(r);
    r = OTF2_AttributeList_AddUint64(self->attributes, attr_unique_id, self->id);
    CHECK_OTF2_ERROR_CODE(r);
    r = OTF2_AttributeList_AddStringRef(self->attributes, attr_thread_type,
        self->thread_type == otter_thread_initial ? 
            attr_label_ref[attr_thread_type_initial] :
        self->thread_type == otter_thread_worker ? 
            attr_label_ref[attr_thread_type_worker] : 0);
    CHECK_OTF2_ERROR_CODE(r);
    return;
}

void
trace_write_location_definition(trace_location_def_t *loc)
{
    if (loc == NULL)
    {
        LOG_ERROR("null pointer");
        return;
    }

    char location_name[DEFAULT_NAME_BUF_SZ + 1] = {0};
    OTF2_StringRef location_name_ref = get_unique_str_ref();
    snprintf(location_name, DEFAULT_NAME_BUF_SZ, "Thread %lu", loc->id);

    LOG_DEBUG("[t=%lu] locking global def writer", loc->id);
    pthread_mutex_t *def_writer_lock = global_def_writer_lock();
    pthread_mutex_lock(def_writer_lock);

    OTF2_GlobalDefWriter *Defs = get_global_def_writer();
    OTF2_GlobalDefWriter_WriteString(Defs,
        location_name_ref,
        location_name);

    LOG_DEBUG("[t=%lu] writing location definition", loc->id);
    OTF2_GlobalDefWriter_WriteLocation(Defs,
        loc->ref,
        location_name_ref,
        loc->type,
        loc->events,
        loc->location_group);

    LOG_DEBUG("[t=%lu] unlocking global def writer", loc->id);
    pthread_mutex_unlock(def_writer_lock);

    return;
}

/**
 * @brief Get the next region definition in the location's queue.
 */
bool
trace_location_get_region_def(trace_location_def_t *loc, trace_region_def_t **rgn) {
    return queue_pop(loc->rgn_defs, (data_item_t*) rgn);
}

bool
trace_location_store_region_def(trace_location_def_t *loc, trace_region_def_t *rgn) {
    /* Add region definition to location's region definition queue */
    return queue_push(loc->rgn_defs, (data_item_t) {.ptr = rgn});
}

size_t
trace_location_get_num_region_def(trace_location_def_t *loc) {
    return queue_length(loc->rgn_defs);
}

unique_id_t
trace_location_get_id(trace_location_def_t *loc) {
    return loc->id;
}

void
trace_location_get_otf2(
    trace_location_def_t *loc,
    OTF2_AttributeList **attributes,
    OTF2_EvtWriter **evt_writer,
    OTF2_DefWriter **def_writer)
{
    if (attributes) *attributes = loc->attributes;
    if (evt_writer) *evt_writer = loc->evt_writer;
    if (def_writer) *def_writer = loc->def_writer;
    return;
}

void
trace_location_inc_event_count(trace_location_def_t *loc)
{
    loc->events++;
    return;
}

/**
 * @brief Indicate to a location that it is entering a new region which will
 * inherit from this location all region definitions it encounters inside this
 * region.
 * 
 * Definitions for regions encountered inside this region will be stored and
 * should be handed off to said region upon leaving it.
 * 
 * @param loc 
 */
void
trace_location_enter_region_def_scope(trace_location_def_t *loc)
{
    stack_push(loc->rgn_defs_stack, (data_item_t) {.ptr = loc->rgn_defs});
    loc->rgn_defs = queue_create();
    return;
}

/**
 * @brief Indicate to a location that it is leaving a region which inherits all
 * region definitions stored by the location during this region.
 * 
 * @param loc 
 * @param rgn 
 */
void
trace_location_leave_region_def_scope(trace_location_def_t *loc, trace_region_def_t *rgn)
{
    if (!queue_append(trace_region_get_rgn_def_queue(rgn), loc->rgn_defs)) {
        LOG_ERROR("error appending items to queue");
    }
    queue_destroy(loc->rgn_defs, false, NULL);
    stack_pop(loc->rgn_defs_stack, (data_item_t*) &loc->rgn_defs);
}

void
trace_location_enter_region(trace_location_def_t *loc, trace_region_def_t *rgn)
{
    stack_push(loc->rgn_stack, (data_item_t) {.ptr = rgn});
}

void
trace_location_leave_region(trace_location_def_t *loc, trace_region_def_t **rgn)
{
    if (stack_is_empty(loc->rgn_stack))
    {
        LOG_ERROR("stack is empty");
        abort();
    }
    stack_pop(loc->rgn_stack, (data_item_t*) rgn);
}

void
trace_location_get_active_regions_from_task(trace_location_def_t *loc, trace_region_def_t *task)
{
    // Only valid if task is a task region
    otter_stack_t *dest = loc->rgn_stack;
    otter_stack_t *src  = trace_region_get_task_rgn_stack(task);
    LOG_ERROR_IF(
        (stack_is_empty(dest) == false),
        "location's region stack not empty"
    );
    stack_transfer(dest, src);
    return;
}

void
trace_location_store_active_regions_in_task(trace_location_def_t *loc, trace_region_def_t *task)
{
    // Only valid if task is a task region
    otter_stack_t *dest = trace_region_get_task_rgn_stack(task);
    otter_stack_t *src  = loc->rgn_stack;
    LOG_ERROR_IF(
        (stack_is_empty(dest) == false),
        "task's region stack not empty"
    );
    stack_transfer(dest, src);
    return;
}
