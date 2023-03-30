// TODO: this file contains events for the OMPT event model as well as functions for adding region attributes to an attribute list and writing a region definition to a trace. Probably want to decouple these.

// TODO: fix pointer to incomplete type (use getters instead)

#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sched.h>

#include <otf2/otf2.h>
#include <otf2/OTF2_Pthread_Locks.h>

#include "public/debug.h"
#include "public/types/queue.h"
#include "public/types/stack.h"
#include "public/otter-common.h"
#include "public/otter-environment-variables.h"
#include "public/otter-trace/trace.h"
#include "public/otter-trace/trace-location.h"

#include "src/otter-trace/trace-timestamp.h"
#include "src/otter-trace/trace-attributes.h"
#include "src/otter-trace/trace-archive.h"
#include "src/otter-trace/trace-lookup-macros.h"
#include "src/otter-trace/trace-unique-refs.h"
#include "src/otter-trace/trace-check-error-code.h"
#include "src/otter-trace/trace-static-constants.h"
#include "src/otter-trace/trace-common-event-attributes.h"

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*   WRITE DEFINITIONS                                                       */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// TODO: this is used by otter-ompt and otter-serial, so want to allow passing in state here
void
trace_write_region_definition(trace_region_def_t *rgn)
{
    trace_region_write_definition_impl(get_global_def_writer(), rgn);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/*   WRITE EVENTS                                                            */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

// TODO: lots of access of trace_location_def_t and trace_region_def_t internals to factor out

void
trace_event_thread_begin(trace_location_def_t *self)
{
    OTF2_AttributeList *attributes = NULL;
    OTF2_EvtWriter *evt_writer = NULL;
    OTF2_DefWriter *def_writer = NULL;
    trace_location_get_otf2(self, &attributes, &evt_writer, &def_writer);

    trace_add_thread_attributes(self);
    OTF2_AttributeList_AddStringRef(
        attributes,
        attr_event_type,
        attr_label_ref[attr_event_type_thread_begin]
    );
    OTF2_AttributeList_AddStringRef(
        attributes,
        attr_endpoint,
        attr_label_ref[attr_endpoint_enter]
    );
    OTF2_EvtWriter_ThreadBegin(
        evt_writer,
        attributes,
        get_timestamp(),
        OTF2_UNDEFINED_COMM,
        trace_location_get_id(self)
    );
    trace_location_inc_event_count(self);
    return;
}

void
trace_event_thread_end(trace_location_def_t *self)
{
    OTF2_AttributeList *attributes = NULL;
    OTF2_EvtWriter *evt_writer = NULL;
    OTF2_DefWriter *def_writer = NULL;
    trace_location_get_otf2(self, &attributes, &evt_writer, &def_writer);

    trace_add_thread_attributes(self);
    OTF2_AttributeList_AddStringRef(
        attributes,
        attr_event_type,
        attr_label_ref[attr_event_type_thread_end]
    );
    OTF2_AttributeList_AddStringRef(
        attributes,
        attr_endpoint,
        attr_label_ref[attr_endpoint_leave]
    );
    OTF2_EvtWriter_ThreadEnd(
        evt_writer,    
        attributes, 
        get_timestamp(), 
        OTF2_UNDEFINED_COMM, 
        trace_location_get_id(self)
    );
    trace_location_inc_event_count(self);
    return;
}

void
trace_event_enter(
    trace_location_def_t *self,
    trace_region_def_t *region)
{
    LOG_ERROR_IF((region == NULL), "null region pointer");

    OTF2_EvtWriter *evt_writer = NULL;
    trace_location_get_otf2(self, NULL, &evt_writer, NULL);

    if (region->type == trace_region_parallel)
    {
        trace_location_enter_region_def_scope(self);

        // TODO: move into trace-region-def.c e.g. trace_region_parallel_lock(region);
        /* Parallel regions must be accessed atomically as they are shared 
           between threads */
        pthread_mutex_lock(&region->attr.parallel.lock_rgn);
    }

    /* Add attributes common to all enter/leave events */
    trace_add_common_event_attributes(
        trace_region_get_attribute_list(region),
        trace_region_get_encountering_task_id(region),
        trace_region_get_type(region),
        trace_region_get_attributes(region)
    );

    OTF2_AttributeList *attributes = trace_region_get_attribute_list(region);

    /* Add the event type attribute */
    OTF2_AttributeList_AddStringRef(attributes, attr_event_type,
        region->type == trace_region_parallel ?
            attr_label_ref[attr_event_type_parallel_begin] :
        region->type == trace_region_workshare ?
            attr_label_ref[attr_event_type_workshare_begin] :
        region->type == trace_region_synchronise ?
            attr_label_ref[attr_event_type_sync_begin] :
        region->type == trace_region_master ?
            attr_label_ref[attr_event_type_master_begin] :
        region->type == trace_region_phase ?
            attr_label_ref[attr_event_type_phase_begin] :
        attr_label_ref[attr_event_type_task_enter]
    );

    /* Add the endpoint */
    OTF2_AttributeList_AddStringRef(attributes, attr_endpoint,
        attr_label_ref[attr_endpoint_enter]);

    /* Add region's attributes to the event */
    switch (region->type) {
    case trace_region_parallel: trace_add_parallel_attributes(region); break;
    case trace_region_workshare: trace_add_workshare_attributes(region); break;
    case trace_region_synchronise: trace_add_sync_attributes(region); break;
    case trace_region_task: trace_add_task_attributes(region); break;
    case trace_region_master: trace_add_master_attributes(region); break;
    case trace_region_phase: trace_add_phase_attributes(region); break;
    default:
        LOG_ERROR("unhandled region type %d", region->type);
        abort();
    }
    
    /* Record the event */
    OTF2_EvtWriter_Enter(evt_writer, attributes, get_timestamp(), region->ref);

    trace_location_enter_region(self, region);

    if (region->type == trace_region_parallel)
    {
        // TODO: move into trace-region-def.c e.g. trace_region_parallel_inc_refcount(region) and trace_region_parallel_unlock(region);
        region->attr.parallel.ref_count++;
        region->attr.parallel.enter_count++;
        pthread_mutex_unlock(&region->attr.parallel.lock_rgn);
    }

    trace_location_inc_event_count(self);
    return;
}

void
trace_event_leave(trace_location_def_t *self)
{
    trace_region_def_t *region = NULL;
    trace_location_leave_region(self, &region);

    OTF2_EvtWriter *evt_writer = NULL;
    trace_location_get_otf2(self, NULL, &evt_writer, NULL);

    if (region->type == trace_region_parallel)
    {
        // TODO: move into trace-region-def.c
        /* Parallel regions must be accessed atomically as they are shared 
           between threads */
        pthread_mutex_lock(&region->attr.parallel.lock_rgn);
    }

    /* Add attributes common to all enter/leave events */
    trace_add_common_event_attributes(
        trace_region_get_attribute_list(region),
        trace_region_get_encountering_task_id(region),
        trace_region_get_type(region),
        trace_region_get_attributes(region)
    );

    OTF2_AttributeList *attributes = trace_region_get_attribute_list(region);

    /* Add the event type attribute */
    OTF2_AttributeList_AddStringRef(attributes, attr_event_type,
        region->type == trace_region_parallel ?
            attr_label_ref[attr_event_type_parallel_end] :
        region->type == trace_region_workshare ?
            attr_label_ref[attr_event_type_workshare_end] :
        region->type == trace_region_synchronise ?
            attr_label_ref[attr_event_type_sync_end] :
        region->type == trace_region_master ? 
            attr_label_ref[attr_event_type_master_end] :
        region->type == trace_region_phase ?
            attr_label_ref[attr_event_type_phase_end] :
        attr_label_ref[attr_event_type_task_leave]
    );

    /* Add the endpoint */
    OTF2_AttributeList_AddStringRef(attributes, attr_endpoint,
        attr_label_ref[attr_endpoint_leave]);

    /* Add region's attributes to the event */
    switch (region->type) {
    case trace_region_parallel: trace_add_parallel_attributes(region); break;
    case trace_region_workshare: trace_add_workshare_attributes(region); break;
    case trace_region_synchronise: trace_add_sync_attributes(region); break;
    case trace_region_task: trace_add_task_attributes(region); break;
    case trace_region_master: trace_add_master_attributes(region); break;
    case trace_region_phase: trace_add_phase_attributes(region); break;
    default:
        LOG_ERROR("unhandled region type %d", region->type);
        abort();
    }

    /* Record the event */
    OTF2_EvtWriter_Leave(evt_writer, attributes, get_timestamp(), region->ref);
    
    /* Parallel regions must be cleaned up by the last thread to leave */
    if (region->type == trace_region_parallel)
    {
        trace_location_leave_region_def_scope(self, region);

        // TODO: move into trace-region-def.c -> e.g. trace_region_finalise_parallel(region);
        region->attr.parallel.ref_count--;

        /* Check the ref count atomically __before__ unlocking */
        if (region->attr.parallel.ref_count == 0)
        {
            pthread_mutex_unlock(&region->attr.parallel.lock_rgn);
            trace_destroy_parallel_region(region);
        } else {
            pthread_mutex_unlock(&region->attr.parallel.lock_rgn);
        }
    }
    trace_location_inc_event_count(self);
    return;
}

void
trace_event_task_create(
    trace_location_def_t *self, 
    trace_region_def_t   *region)
{
    OTF2_EvtWriter *evt_writer = NULL;
    trace_location_get_otf2(self, NULL, &evt_writer, NULL);

    trace_add_common_event_attributes(
        trace_region_get_attribute_list(region),
        trace_region_get_encountering_task_id(region),
        trace_region_get_type(region),
        trace_region_get_attributes(region)
    );

    /* task-create */
    OTF2_AttributeList_AddStringRef(region->attributes, attr_event_type,
        attr_label_ref[attr_event_type_task_create]);

    /* discrete event (no duration) */
    OTF2_AttributeList_AddStringRef(
        region->attributes,
        attr_endpoint,
        attr_label_ref[attr_endpoint_discrete]
    );

    /* return address */
    OTF2_AttributeList_AddUint64(region->attributes, attr_task_create_ra,
// https://releases.llvm.org/15.0.0/tools/clang/docs/ReleaseNotes.html#improvements-to-clang-s-diagnostics
// The -Wint-conversion warning diagnostic for implicit int <-> pointer conversions now defaults to an error in all C language modes.
        (uint64_t) region->attr.task.task_create_ra
    );

    trace_add_task_attributes(region);
    
    OTF2_EvtWriter_ThreadTaskCreate(
        evt_writer,
        region->attributes,
        get_timestamp(),
        OTF2_UNDEFINED_COMM,
        OTF2_UNDEFINED_UINT32, 0); /* creating thread, generation number */
    trace_location_inc_event_count(self);
    return;
}

void 
trace_event_task_schedule(
    trace_location_def_t    *self,
    trace_region_def_t      *prior_task,
    otter_task_status_t      prior_status)
{
    /* Update prior task's status before recording task enter/leave events */
    LOG_ERROR_IF((prior_task->type != trace_region_task),
        "invalid region type %d", prior_task->type);
    prior_task->attr.task.task_status = prior_status;
    return;
}

void
trace_event_task_switch(
    trace_location_def_t *self, 
    trace_region_def_t   *prior_task, 
    otter_task_status_t   prior_status, 
    trace_region_def_t   *next_task)
{
    // Update prior task's status
    // Transfer thread's active region stack to prior_task->rgn_stack
    // Transfer next_task->rgn_stack to thread
    // Record event with details of tasks swapped & prior_status
    
    OTF2_EvtWriter *evt_writer = NULL;
    trace_location_get_otf2(self, NULL, &evt_writer, NULL);

    prior_task->attr.task.task_status = prior_status;

    trace_location_store_active_regions_in_task(self, prior_task);
    trace_location_get_active_regions_from_task(self, next_task);

    trace_add_common_event_attributes(
        trace_region_get_attribute_list(prior_task),
        trace_region_get_encountering_task_id(prior_task),
        trace_region_get_type(prior_task),
        trace_region_get_attributes(prior_task)
    );

    // Record the reason the task-switch event ocurred
    OTF2_AttributeList_AddStringRef(
        prior_task->attributes,
        attr_prior_task_status,
        TASK_STATUS_TO_STR_REF(prior_status)
    );

    // The task that was suspended
    OTF2_AttributeList_AddUint64(
        prior_task->attributes,
        attr_prior_task_id,
        prior_task->attr.task.id
    );

    // The task that was resumed
    OTF2_AttributeList_AddUint64(
        prior_task->attributes,
        attr_unique_id,
        next_task->attr.task.id
    );

    // The task that was resumed
    OTF2_AttributeList_AddUint64(
        prior_task->attributes,
        attr_next_task_id,
        next_task->attr.task.id
    );

    // The region_type of the task that was resumed
    OTF2_AttributeList_AddStringRef(
        prior_task->attributes,
        attr_next_task_region_type,
        TASK_TYPE_TO_STR_REF(next_task->attr.task.type)
    );

    // Task-switch is always considered a discrete event
    OTF2_AttributeList_AddStringRef(
        prior_task->attributes,
        attr_endpoint,
        attr_label_ref[attr_endpoint_discrete]
    );

    OTF2_AttributeList_AddStringRef(
        prior_task->attributes,
        attr_event_type,
        attr_label_ref[attr_event_type_task_switch]
    );

    OTF2_EvtWriter_ThreadTaskSwitch(
        evt_writer,
        prior_task->attributes,
        get_timestamp(),
        OTF2_UNDEFINED_COMM,
        OTF2_UNDEFINED_UINT32, 0); /* creating thread, generation number */

    return;
}
