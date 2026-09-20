/* SPDX-License-Identifier: LGPL-2.1-or-later */
#pragma once

#include "forward.h"

/* The enum order is used to order unit jobs in the job queue
 * when other criteria (cpu weight, nice level) are identical.
 * In this case service units have the highest priority. */
typedef enum UnitType {
        UNIT_SERVICE,
        UNIT_MOUNT,
        UNIT_SWAP,
        UNIT_SOCKET,
        UNIT_TARGET,
        UNIT_DEVICE,
        UNIT_AUTOMOUNT,
        UNIT_TIMER,
        UNIT_PATH,
        UNIT_SLICE,
        UNIT_SCOPE,
        _UNIT_TYPE_MAX,
        _UNIT_TYPE_INVALID = -EINVAL,
        _UNIT_TYPE_ERRNO_MAX = -ERRNO_MAX, /* Ensure the whole errno range fits into this enum */
} UnitType;

typedef enum UnitActiveState {
        UNIT_ACTIVE,
        UNIT_RELOADING,
        UNIT_INACTIVE,
        UNIT_FAILED,
        UNIT_ACTIVATING,
        UNIT_DEACTIVATING,
        UNIT_MAINTENANCE,
        UNIT_REFRESHING,
        _UNIT_ACTIVE_STATE_MAX,
        _UNIT_ACTIVE_STATE_INVALID = -EINVAL,
} UnitActiveState;

typedef enum UnitDependency {
        /* Positive dependencies */
        UNIT_REQUIRES,
        UNIT_REQUISITE,
        UNIT_WANTS,
        UNIT_BINDS_TO,
        UNIT_PART_OF,
        UNIT_UPHOLDS,

        /* Inverse of the above */
        UNIT_REQUIRED_BY,             /* inverse of 'requires' is 'required_by' */
        UNIT_REQUISITE_OF,            /* inverse of 'requisite' is 'requisite_of' */
        UNIT_WANTED_BY,               /* inverse of 'wants' */
        UNIT_BOUND_BY,                /* inverse of 'binds_to' */
        UNIT_CONSISTS_OF,             /* inverse of 'part_of' */
        UNIT_UPHELD_BY,               /* inverse of 'uphold' */

        /* Negative dependencies */
        UNIT_CONFLICTS,               /* inverse of 'conflicts' is 'conflicted_by' */
        UNIT_CONFLICTED_BY,

        /* Order */
        UNIT_BEFORE,                  /* inverse of 'before' is 'after' and vice versa */
        UNIT_AFTER,

        /* OnSuccess= + OnFailure= */
        UNIT_ON_SUCCESS,
        UNIT_ON_SUCCESS_OF,
        UNIT_ON_FAILURE,
        UNIT_ON_FAILURE_OF,

        /* Triggers (i.e. a socket triggers a service) */
        UNIT_TRIGGERS,
        UNIT_TRIGGERED_BY,

        /* Propagate reloads */
        UNIT_PROPAGATES_RELOAD_TO,
        UNIT_RELOAD_PROPAGATED_FROM,

        /* Propagate stops */
        UNIT_PROPAGATES_STOP_TO,
        UNIT_STOP_PROPAGATED_FROM,

        /* Joins namespace of */
        UNIT_JOINS_NAMESPACE_OF,

        /* Reference information for GC logic */
        UNIT_REFERENCES,              /* Inverse of 'references' is 'referenced_by' */
        UNIT_REFERENCED_BY,

        /* Slice= */
        UNIT_IN_SLICE,
        UNIT_SLICE_OF,

        _UNIT_DEPENDENCY_MAX,
        _UNIT_DEPENDENCY_INVALID = -EINVAL,
} UnitDependency;

typedef enum JobMode {
        JOB_FAIL,                 /* Fail if a conflicting job is already queued */
        JOB_LENIENT,              /* Fail if any conflicting unit is active (even weaker than JOB_FAIL) */
        JOB_REPLACE,              /* Replace an existing conflicting job */
        JOB_REPLACE_IRREVERSIBLY, /* Like JOB_REPLACE + produce irreversible jobs */
        JOB_ISOLATE,              /* Start a unit, and stop all others */
        JOB_FLUSH,                /* Flush out all other queued jobs when queueing this one */
        JOB_IGNORE_DEPENDENCIES,  /* Ignore both requirement and ordering dependencies */
        JOB_IGNORE_REQUIREMENTS,  /* Ignore requirement dependencies */
        JOB_TRIGGERING,           /* Adds TRIGGERED_BY dependencies to the same transaction */
        JOB_RESTART_DEPENDENCIES, /* A "start" job for the specified unit becomes "restart" for depending units */
        _JOB_MODE_MAX,
        _JOB_MODE_INVALID = -EINVAL,
} JobMode;

DECLARE_STRING_TABLE_LOOKUP(unit_type, UnitType);
