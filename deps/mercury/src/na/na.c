/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "na_plugin.h"

#include "mercury_atomic_queue.h"
#include "mercury_mem.h"
#ifdef NA_HAS_MULTI_PROGRESS
#    include "mercury_thread_condition.h"
#    include "mercury_thread_mutex.h"
#endif
#include "mercury_thread_spin.h"
#ifdef NA_HAS_DYNAMIC_PLUGINS
#    include "mercury_dl.h"
#    ifdef _WIN32
#        include <Windows.h>
#    else
#        include <dirent.h>
#    endif
#endif

#include <stdlib.h>
#include <string.h>

/****************/
/* Local Macros */
/****************/

/* Name of this subsystem */
#define NA_SUBSYS_NAME        na
#define NA_STRINGIFY(x)       HG_UTIL_STRINGIFY(x)
#define NA_SUBSYS_NAME_STRING NA_STRINGIFY(NA_SUBSYS_NAME)

#define NA_CLASS_DELIMITER     "+" /* e.g. "class+protocol" */
#define NA_CLASS_DELIMITER_LEN (1)

#ifdef _WIN32
#    define strtok_r strtok_s
#    undef strdup
#    define strdup _strdup
#endif

#define NA_ATOMIC_QUEUE_SIZE 1024 /* TODO make it configurable */

/* 32-bit lock value for serial progress */
#define NA_PROGRESS_LOCK 0x80000000

/* Dynamic plugins */
#define NA_PLUGIN_PREFIX   "libna_plugin_"
#define NA_PLUGIN_SCN_NAME "libna_plugin_%16[^_.]"
#define NA_PLUGIN_PATH_MAX (1024)
#define NA_PLUGIN_NAME_MAX (16)

/* Max number of NA plugins */
#define NA_PLUGIN_MAX (16)

/************************************/
/* Local Type and Struct Definition */
/************************************/

#ifdef NA_HAS_DYNAMIC_PLUGINS
/* Plugin entries */
struct na_plugin_entry {
    char *path;
    HG_DL_HANDLE dl_handle;
    const struct na_class_ops *ops;
};
#endif

/* Private class */
struct na_private_class {
    struct na_class na_class; /* Must remain as first field */
};

/* Completion queue */
struct na_completion_queue {
    STAILQ_HEAD(, na_cb_completion_data) queue; /* Completion queue */
    hg_thread_spin_t lock;                      /* Completion queue lock */
    hg_atomic_int32_t count;                    /* Number of entries */
};

#ifdef NA_HAS_MULTI_PROGRESS
/* Ensure thread safety when progressing context from multiple threads */
struct na_progress_multi {
    hg_thread_cond_t cond;   /* Cond */
    hg_thread_mutex_t mutex; /* Mutex */
    hg_atomic_int32_t count; /* Count */
};
#endif

/* Private context / do not expose private members to plugins */
struct na_private_context {
    struct na_context context; /* Must remain as first field */
#ifdef NA_HAS_MULTI_PROGRESS
    struct na_progress_multi progress_multi; /* Progress multi */
#endif
    struct na_completion_queue backfill_queue; /* Backfill queue */
    struct hg_atomic_queue *completion_queue;  /* Default completion queue */
    na_class_t *na_class;                      /* Pointer to NA class */
};

/* NA address */
struct na_addr;

/* NA memory handle */
struct na_mem_handle;

/* NA op ID */
struct na_op_id;

/********************/
/* Local Prototypes */
/********************/

/* Parse host string and fill info */
static na_return_t
na_info_parse(
    const char *host_string, char **class_name_p, struct na_info **na_info_p);

/* Free host info */
static void
na_info_free(struct na_info *na_info);

/* Get protocol info from plugins */
static na_return_t
na_plugin_get_protocol_info(const struct na_class_ops *const class_ops[],
    const char *class_name, const struct na_info *na_info,
    struct na_protocol_info **na_protocol_info_p);

/* Attempt to find a suitable plugin */
static na_return_t
na_plugin_check_protocol(const struct na_class_ops *const class_ops[],
    const char *class_name, const char *protocol_name,
    const struct na_class_ops **ops_p);

#ifdef NA_HAS_DYNAMIC_PLUGINS
/* Scan a given path and return a list of plugins */
static na_return_t
na_plugin_scan_path(const char *path, struct na_plugin_entry **entries_p);

#    ifndef _WIN32
/* Filter plugin path names */
static int
na_plugin_filter(const struct dirent *entry);
#    endif

/* Close all opened plugins */
static void
na_plugin_close_all(struct na_plugin_entry *entries);

/* Open plugin in a given path */
static na_return_t
na_plugin_open(
    const char *path, const char *file, struct na_plugin_entry *entry);

/* Close plugin entry */
static void
na_plugin_close(struct na_plugin_entry *entry);
#endif /* NA_HAS_DYNAMIC_PLUGINS */

/* Busy wait using poll */
static na_return_t
na_poll_busy_wait(
    na_class_t *na_class, na_context_t *context, unsigned int timeout_ms);

/*******************/
/* Local Variables */
/*******************/

/* Static plugin ops table */
static const struct na_class_ops *const na_plugin_static_g[] = {
#ifdef NA_HAS_SM
    &NA_PLUGIN_OPS(sm), /* Keep NA SM first for protocol selection */
#endif
#ifndef NA_HAS_DYNAMIC_PLUGINS
#    ifdef NA_HAS_OFI
    &NA_PLUGIN_OPS(ofi),
#    endif
#    ifdef NA_HAS_UCX
    &NA_PLUGIN_OPS(ucx),
#    endif
#endif
#ifdef NA_HAS_BMI
    &NA_PLUGIN_OPS(bmi),
#endif
#ifdef NA_HAS_MPI
    &NA_PLUGIN_OPS(mpi),
#endif
#ifdef NA_HAS_PSM
    &NA_PLUGIN_OPS(psm),
#endif
#ifdef NA_HAS_PSM2
    &NA_PLUGIN_OPS(psm2),
#endif
    NULL};

/* Dynamic plugin ops table */
#ifdef NA_HAS_DYNAMIC_PLUGINS
static struct na_plugin_entry *na_plugin_dynamic_g = NULL;
#endif

/* Return code string table */
#define X(a) #a,
static const char *const na_return_name_g[] = {NA_RETURN_VALUES};
#undef X

/* Callback type string table */
#define X(a) #a,
static const char *const na_cb_type_name_g[] = {NA_CB_TYPES};
#undef X

/* Default log outlets */
#ifdef _WIN32
HG_LOG_OUTLET_DECL(NA_SUBSYS_NAME) = HG_LOG_OUTLET_INITIALIZER(
    NA_SUBSYS_NAME, HG_LOG_PASS, NULL, NULL);
#else
/* NA_LOG_DEBUG_LESIZE: default number of debug log entries. */
#    define NA_LOG_DEBUG_LESIZE (256)

/* Declare debug log for na */
static HG_LOG_DEBUG_DECL_LE(NA_SUBSYS_NAME, NA_LOG_DEBUG_LESIZE);
static HG_LOG_DEBUG_DECL_DLOG(NA_SUBSYS_NAME) = HG_LOG_DLOG_INITIALIZER(
    NA_SUBSYS_NAME, NA_LOG_DEBUG_LESIZE);

HG_LOG_DLOG_DECL_REGISTER(NA_SUBSYS_NAME);
#endif
HG_LOG_SUBSYS_DECL_STATE_REGISTER(fatal, NA_SUBSYS_NAME, HG_LOG_ON);

/* Specific log outlets */
NA_PLUGIN_VISIBILITY HG_LOG_SUBSYS_DECL_REGISTER(cls, NA_SUBSYS_NAME);
HG_LOG_SUBSYS_DECL_REGISTER(ctx, NA_SUBSYS_NAME);
HG_LOG_SUBSYS_DECL_REGISTER(op, NA_SUBSYS_NAME);
HG_LOG_SUBSYS_DECL_REGISTER(addr, NA_SUBSYS_NAME);
HG_LOG_SUBSYS_DECL_REGISTER(msg, NA_SUBSYS_NAME);
HG_LOG_SUBSYS_DECL_REGISTER(mem, NA_SUBSYS_NAME);
HG_LOG_SUBSYS_DECL_REGISTER(rma, NA_SUBSYS_NAME);
HG_LOG_SUBSYS_DECL_REGISTER(poll, NA_SUBSYS_NAME);

/* Off by default because of potientally excessive logs */
HG_LOG_SUBSYS_DECL_STATE_REGISTER(poll_loop, NA_SUBSYS_NAME, HG_LOG_OFF);
HG_LOG_SUBSYS_DECL_STATE_REGISTER(ip, NA_SUBSYS_NAME, HG_LOG_OFF);
HG_LOG_SUBSYS_DECL_STATE_REGISTER(perf, NA_SUBSYS_NAME, HG_LOG_OFF);

/* Extra log outlet for libfabric / ucx */
HG_LOG_SUBSYS_DECL_STATE_REGISTER(libfabric, NA_SUBSYS_NAME, HG_LOG_OFF);
HG_LOG_SUBSYS_DECL_STATE_REGISTER(ucx, NA_SUBSYS_NAME, HG_LOG_OFF);

#ifdef NA_HAS_DYNAMIC_PLUGINS
/* Initialize list of plugins etc */
static void
na_initialize(void) NA_CONSTRUCTOR;

/* Free list of plugins etc */
static void
na_finalize(void) NA_DESTRUCTOR;
#endif

/*---------------------------------------------------------------------------*/
#ifdef NA_HAS_DYNAMIC_PLUGINS
static void
na_initialize(void)
{
    const char *plugin_path = getenv("NA_PLUGIN_PATH");
    na_return_t ret;

    if (plugin_path == NULL)
        plugin_path = NA_DEFAULT_PLUGIN_PATH;

    ret = na_plugin_scan_path(plugin_path, &na_plugin_dynamic_g);
    NA_CHECK_SUBSYS_WARNING(fatal, ret != NA_SUCCESS,
        "No plugin found in path (%s), consider setting NA_PLUGIN_PATH.",
        plugin_path);
}

/*---------------------------------------------------------------------------*/
static void
na_finalize(void)
{
    na_plugin_close_all(na_plugin_dynamic_g);
}
#endif

/*---------------------------------------------------------------------------*/
static na_return_t
na_info_parse(
    const char *info_string, char **class_name_p, struct na_info **na_info_p)
{
    char *class_name = NULL;
    struct na_info *na_info = NULL;
    char *input_string = NULL, *token = NULL, *locator = NULL;
    na_return_t ret = NA_SUCCESS;

    na_info = (struct na_info *) malloc(sizeof(struct na_info));
    NA_CHECK_SUBSYS_ERROR(cls, na_info == NULL, error, ret, NA_NOMEM,
        "Could not allocate NA info struct");
    *na_info = (struct na_info){.host_name = NULL,
        .protocol_name = NULL,
        .na_init_info = NA_INIT_INFO_INITIALIZER};

    /* Copy info string and work from that */
    input_string = strdup(info_string);
    NA_CHECK_SUBSYS_ERROR(cls, input_string == NULL, error, ret, NA_NOMEM,
        "Could not duplicate host string");

    /**
     * Strings can be of the format:
     *   [<class>+]<protocol>[://[<host string>]]
     */

    /* Get first part of string (i.e., class_name+protocol) */
    token = strtok_r(input_string, ":", &locator);

    /* Is class name specified */
    if (strstr(token, NA_CLASS_DELIMITER) != NULL) {
        char *_locator = NULL;

        token = strtok_r(token, NA_CLASS_DELIMITER, &_locator);

        /* Get NA class name */
        class_name = strdup(token);
        NA_CHECK_SUBSYS_ERROR(cls, class_name == NULL, error, ret, NA_NOMEM,
            "Could not duplicate NA info class name");

        /* Get protocol name */
        na_info->protocol_name = strdup(_locator);
        NA_CHECK_SUBSYS_ERROR(cls, na_info->protocol_name == NULL, error, ret,
            NA_NOMEM, "Could not duplicate NA info protocol name");
    } else {
        /* Get protocol name */
        na_info->protocol_name = strdup(token);
        NA_CHECK_SUBSYS_ERROR(cls, na_info->protocol_name == NULL, error, ret,
            NA_NOMEM, "Could not duplicate NA info protocol name");
    }

    /* Is the host string empty? */
    if (!locator || locator[0] == '\0')
        goto done;

    /* Format sanity check ("://") */
    NA_CHECK_SUBSYS_ERROR(fatal, strncmp(locator, "//", 2) != 0, error, ret,
        NA_PROTONOSUPPORT, "Bad address string format");

    /* :// followed by empty hostname is allowed, explicitly check here */
    if (locator[2] == '\0')
        goto done;

    na_info->host_name = strdup(locator + 2);
    NA_CHECK_SUBSYS_ERROR(cls, na_info->host_name == NULL, error, ret, NA_NOMEM,
        "Could not duplicate NA info host name");

done:
    *class_name_p = class_name;
    *na_info_p = na_info;
    free(input_string);

    return ret;

error:
    free(class_name);
    na_info_free(na_info);
    free(input_string);

    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_info_free(struct na_info *na_info)
{
    if (!na_info)
        return;

    free(na_info->protocol_name);
    free(na_info->host_name);
    free(na_info);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_plugin_get_protocol_info(const struct na_class_ops *const class_ops[],
    const char *class_name, const struct na_info *na_info,
    struct na_protocol_info **na_protocol_info_p)
{
    const struct na_class_ops *ops;
    struct na_protocol_info *na_protocol_info = NULL;
    na_return_t ret;
    int i;

    /* Parse plugins */
    for (i = 0, ops = class_ops[0]; ops != NULL; i++, ops = class_ops[i]) {
        struct na_protocol_info *plugin_protocol_info = NULL, *tail = NULL;

        NA_CHECK_SUBSYS_ERROR(cls, ops->class_name == NULL, error, ret,
            NA_PROTONOSUPPORT, "class name is not defined");
        NA_CHECK_SUBSYS_ERROR(cls, ops->check_protocol == NULL, error, ret,
            NA_OPNOTSUPPORTED, "check_protocol plugin callback is not defined");

        /* Skip check protocol if class name does not match */
        if ((class_name != NULL &&
                (strcmp(ops->class_name, class_name) != 0)) ||
            ops->get_protocol_info == NULL) {
            NA_LOG_SUBSYS_DEBUG(cls, "Skipping %s", ops->class_name);
            continue;
        }

        ret = ops->get_protocol_info(na_info, &plugin_protocol_info);
        NA_CHECK_SUBSYS_NA_ERROR(
            cls, error, ret, "Could not get protocol info");
        if (plugin_protocol_info == NULL) {
            NA_LOG_SUBSYS_DEBUG(
                cls, "\"%s\" returned no protocol info", ops->class_name);
            continue;
        }

        for (tail = plugin_protocol_info; tail->next != NULL; tail = tail->next)
            /* nothing */;
        tail->next = na_protocol_info;
        na_protocol_info = plugin_protocol_info;
    }

    *na_protocol_info_p = na_protocol_info;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_plugin_check_protocol(const struct na_class_ops *const class_ops[],
    const char *class_name, const char *protocol_name,
    const struct na_class_ops **ops_p)
{
    const struct na_class_ops *ops = NULL;
    na_return_t ret;
    int i;

    for (i = 0, ops = class_ops[0]; ops != NULL; i++, ops = class_ops[i]) {
        NA_CHECK_SUBSYS_ERROR(cls, ops->class_name == NULL, error, ret,
            NA_PROTONOSUPPORT, "class name is not defined");
        NA_CHECK_SUBSYS_ERROR(cls, ops->check_protocol == NULL, error, ret,
            NA_OPNOTSUPPORTED, "check_protocol plugin callback is not defined");

        /* Skip check protocol if class name does not match */
        if ((class_name != NULL) && (strcmp(ops->class_name, class_name) != 0))
            continue;

        /* Check that protocol is supported, if no class name specified, take
         * the first plugin that supports the protocol */
        if (ops->check_protocol(protocol_name))
            break;
        else
            NA_CHECK_SUBSYS_ERROR(fatal, class_name != NULL, error, ret,
                NA_PROTONOSUPPORT,
                "Specified class name \"%s\" does not support requested "
                "protocol",
                class_name);
    }
    *ops_p = ops;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
#ifdef NA_HAS_DYNAMIC_PLUGINS
#    ifdef _WIN32
static na_return_t
na_plugin_scan_path(const char *path, struct na_plugin_entry **entries_p)
{
    na_return_t ret;

    (void) path;
    (void) entries_p;
    NA_GOTO_SUBSYS_ERROR(cls, error, ret, NA_PROTOCOL_ERROR, "Not implemented");

    return NA_SUCCESS;

error:
    return ret;
}

#    else
static na_return_t
na_plugin_scan_path(const char *path, struct na_plugin_entry **entries_p)
{
    struct dirent **plugin_list;
    struct na_plugin_entry *entries = NULL;
    na_return_t ret;
    int n, n_entries = 0;

    n = scandir(path, &plugin_list, na_plugin_filter, alphasort);
    NA_CHECK_SUBSYS_ERROR(
        cls, n < 0, error, ret, NA_FAULT, "scandir(%s) failed", path);

    entries =
        (struct na_plugin_entry *) calloc((size_t) n + 1, sizeof(*entries));
    NA_CHECK_SUBSYS_ERROR(cls, entries == NULL, error, ret, NA_NOMEM,
        "Could not allocate %d plugin entries", n);
    n_entries = n;

    while (n--) {
        ret = na_plugin_open(path, plugin_list[n]->d_name, &entries[n]);
        free(plugin_list[n]);
        NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not open plugin (%s)",
            plugin_list[n]->d_name);
    }

    free(plugin_list);

    *entries_p = entries;

    return NA_SUCCESS;

error:
    if (n > 0) {
        if (entries != NULL) {
            int i;

            /* close entry */
            for (i = n + 1; i < n_entries; i++)
                na_plugin_close(&entries[i]);
            free(entries);
        }

        while (n--)
            free(plugin_list[n]);
        free(plugin_list);
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
static int
na_plugin_filter(const struct dirent *entry)
{
    return !strncmp(entry->d_name, NA_PLUGIN_PREFIX, strlen(NA_PLUGIN_PREFIX));
}
#    endif

/*---------------------------------------------------------------------------*/
static void
na_plugin_close_all(struct na_plugin_entry *entries)
{
    struct na_plugin_entry *entry = NULL;
    int i;

    if (entries == NULL)
        return;

    for (i = 0, entry = &entries[0]; entry->ops != NULL;
         i++, entry = &entries[i])
        na_plugin_close(entry);

    free(entries);
}

/*---------------------------------------------------------------------------*/
static na_return_t
na_plugin_open(
    const char *path, const char *file, struct na_plugin_entry *entry)
{
    char full_path[NA_PLUGIN_PATH_MAX + 1];
    char plugin_name[NA_PLUGIN_NAME_MAX + 1];
    char plugin_ops_name[NA_PLUGIN_NAME_MAX * 2 + 1];
    na_return_t ret;
    int rc;

    /* Generate full path to open plugin */
    rc = snprintf(full_path, sizeof(full_path), "%s/%s", path, file);
    NA_CHECK_SUBSYS_ERROR(cls, rc < 0 || rc > (int) sizeof(full_path), error,
        ret, NA_OVERFLOW,
        "snprintf() failed or name truncated, rc: %d (expected %zu)", rc,
        sizeof(full_path));

    /* Keep a copy of path for debug purposes */
    entry->path = strdup(full_path);
    NA_CHECK_SUBSYS_ERROR(cls, entry->path == NULL, error, ret, NA_NOMEM,
        "Could not dup %s", full_path);

    /* Open plugin */
    NA_LOG_SUBSYS_DEBUG(cls, "Opening plugin %s", entry->path);
    entry->dl_handle = hg_dl_open(entry->path);
    NA_CHECK_SUBSYS_ERROR(cls, entry->dl_handle == NULL, error, ret, NA_NOENTRY,
        "Could not open lib %s (%s)", entry->path, hg_dl_error());

    /* Retrieve plugin name from file name */
    rc = sscanf(file, NA_PLUGIN_SCN_NAME, plugin_name);
    NA_CHECK_SUBSYS_ERROR(cls, rc != 1, error, ret, NA_PROTONOSUPPORT,
        "Could not find plugin name (%s)", file);

    /* Generate plugin ops symbol name */
    rc = snprintf(plugin_ops_name, sizeof(plugin_ops_name), "na_%s_class_ops_g",
        plugin_name);
    NA_CHECK_SUBSYS_ERROR(cls, rc < 0 || rc > (int) sizeof(plugin_ops_name),
        error, ret, NA_OVERFLOW,
        "snprintf() failed or name truncated, rc: %d (expected %zu)", rc,
        sizeof(plugin_ops_name));

    /* Get plugin ops */
    entry->ops = (const struct na_class_ops *) hg_dl_sym(
        entry->dl_handle, plugin_ops_name);
    NA_CHECK_SUBSYS_ERROR(cls, entry->ops == NULL, error, ret, NA_NOENTRY,
        "Could not find symbol %s (%s)", plugin_ops_name, hg_dl_error());

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
static void
na_plugin_close(struct na_plugin_entry *entry)
{
    if (entry->path) {
        NA_LOG_SUBSYS_DEBUG(cls, "Closing plugin %s", entry->path);
        free(entry->path);
    }
    if (entry->dl_handle != NULL)
        (void) hg_dl_close(entry->dl_handle);
    entry->ops = NULL;
}
#endif /* NA_HAS_DYNAMIC_PLUGINS */

/*---------------------------------------------------------------------------*/
static na_return_t
na_poll_busy_wait(
    na_class_t *na_class, na_context_t *context, unsigned int timeout_ms)
{
    hg_time_t deadline, now = hg_time_from_ms(0);
    na_return_t ret;

    if (timeout_ms != 0)
        hg_time_get_current_ms(&now);
    deadline = hg_time_add(now, hg_time_from_ms(timeout_ms));

    do {
        unsigned int count = 0;

        ret = na_class->ops->poll(na_class, context, &count);
        NA_CHECK_SUBSYS_NA_ERROR(poll, error, ret, "Could not poll");

        if (count > 0)
            return NA_SUCCESS;

        if (timeout_ms != 0)
            hg_time_get_current_ms(&now);
    } while (hg_time_less(now, deadline));

    return NA_TIMEOUT;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
void
NA_Version_get(unsigned int *major, unsigned int *minor, unsigned int *patch)
{
    if (major)
        *major = NA_VERSION_MAJOR;
    if (minor)
        *minor = NA_VERSION_MINOR;
    if (patch)
        *patch = NA_VERSION_PATCH;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Get_protocol_info(
    const char *info_string, struct na_protocol_info **na_protocol_info_p)
{
    char *class_name = NULL;
    struct na_info *na_info = NULL;
    struct na_protocol_info *na_protocol_info = NULL;
    na_return_t ret;

    /* If info string is NULL, get info from all plugins */
    if (info_string != NULL) {
        ret = na_info_parse(info_string, &class_name, &na_info);
        NA_CHECK_SUBSYS_NA_ERROR(
            cls, error, ret, "Could not parse host string");
    }

    /* Check list of static plugins */
    ret = na_plugin_get_protocol_info(
        na_plugin_static_g, class_name, na_info, &na_protocol_info);
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not check static plugins");

    /* Check list of dynamic plugins */
#ifdef NA_HAS_DYNAMIC_PLUGINS
    if (na_plugin_dynamic_g != NULL) {
        struct na_protocol_info *dynamic_protocol_info = NULL;
        const struct na_class_ops *na_plugin_dynamic[NA_PLUGIN_MAX] = {NULL};
        const struct na_class_ops *ops = NULL;
        int i;

        for (i = 0, ops = na_plugin_dynamic_g[0].ops;
             i < NA_PLUGIN_MAX && ops != NULL;
             i++, ops = na_plugin_dynamic_g[i].ops)
            na_plugin_dynamic[i] = ops;

        ret = na_plugin_get_protocol_info(
            na_plugin_dynamic, class_name, na_info, &dynamic_protocol_info);
        NA_CHECK_SUBSYS_NA_ERROR(
            cls, error, ret, "Could not check dynamic plugins");
        if (dynamic_protocol_info != NULL) {
            struct na_protocol_info *tail = NULL;

            for (tail = dynamic_protocol_info; tail->next != NULL;
                 tail = tail->next)
                /* nothing */;
            tail->next = na_protocol_info;
            na_protocol_info = dynamic_protocol_info;
        }
    }
#endif

    *na_protocol_info_p = na_protocol_info;

    free(class_name);
    na_info_free(na_info);

    return NA_SUCCESS;

error:
    free(class_name);
    na_info_free(na_info);
    while (na_protocol_info != NULL) {
        struct na_protocol_info *tmp = na_protocol_info;
        na_protocol_info = na_protocol_info->next;
        na_protocol_info_free(tmp);
    }

    return ret;
}

/*---------------------------------------------------------------------------*/
void
NA_Free_protocol_info(struct na_protocol_info *na_protocol_info)
{
    while (na_protocol_info != NULL) {
        struct na_protocol_info *tmp = na_protocol_info;
        na_protocol_info = na_protocol_info->next;
        na_protocol_info_free(tmp);
    }
}

/*---------------------------------------------------------------------------*/
na_class_t *
NA_Initialize(const char *info_string, bool listen)
{
    return NA_Initialize_opt2(info_string, listen, 0, NULL);
}

/*---------------------------------------------------------------------------*/
na_class_t *
NA_Initialize_opt(const char *info_string, bool listen,
    const struct na_init_info *na_init_info)
{
    /* v4.0 is latest version for which init struct was not versioned */
    return NA_Initialize_opt2(
        info_string, listen, NA_VERSION(4, 0), na_init_info);
}

/*---------------------------------------------------------------------------*/
na_class_t *
NA_Initialize_opt2(const char *info_string, bool listen, unsigned int version,
    const struct na_init_info *na_init_info)
{
    struct na_private_class *na_private_class = NULL;
    char *class_name = NULL;
    struct na_info *na_info = NULL;
    const struct na_class_ops *ops = NULL;
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(cls, info_string == NULL, error, ret, NA_INVALID_ARG,
        "NULL info string");

    na_private_class =
        (struct na_private_class *) calloc(1, sizeof(*na_private_class));
    NA_CHECK_SUBSYS_ERROR(cls, na_private_class == NULL, error, ret, NA_NOMEM,
        "Could not allocate class");

    ret = na_info_parse(info_string, &class_name, &na_info);
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not parse host string");

    /* Ensure init info is API compatible */
    if (na_init_info) {
        NA_CHECK_SUBSYS_ERROR(fatal, version == 0, error, ret, NA_INVALID_ARG,
            "API version cannot be 0");
        NA_LOG_SUBSYS_DEBUG(cls, "NA init info version used: v%d.%d",
            NA_MAJOR(version), NA_MINOR(version));

        /* Get init info and overwrite defaults */
        if (NA_VERSION_GE(version, NA_VERSION(5, 0)))
            na_info->na_init_info = *na_init_info;
        else
            na_init_info_dup_4_0(&na_info->na_init_info,
                (const struct na_init_info_4_0 *) na_init_info);

        NA_LOG_SUBSYS_DEBUG(cls,
            "NA Init info: ip_subnet=%s, auth_key=%s, max_unexpected_size=%zu, "
            "max_expected_size=%zu, progress_mode=%" PRIu8
            ", addr_format=%d, max_contexts=%" PRIu8 ", thread_mode=%" PRIu8
            ", request_mem_device=%u, traffic_class=%d",
            na_info->na_init_info.ip_subnet, na_info->na_init_info.auth_key,
            na_info->na_init_info.max_unexpected_size,
            na_info->na_init_info.max_expected_size,
            na_info->na_init_info.progress_mode,
            na_info->na_init_info.addr_format,
            na_info->na_init_info.max_contexts,
            na_info->na_init_info.thread_mode,
            na_info->na_init_info.request_mem_device,
            na_info->na_init_info.traffic_class);

        na_private_class->na_class.progress_mode = na_init_info->progress_mode;
    }

    /* Print debug info */
    NA_LOG_SUBSYS_DEBUG(cls, "Class: %s, Protocol: %s, Hostname: %s",
        class_name, na_info->protocol_name, na_info->host_name);

    /* Check list of static plugins */
    ret = na_plugin_check_protocol(
        na_plugin_static_g, class_name, na_info->protocol_name, &ops);
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not check static plugins");

#ifdef NA_HAS_DYNAMIC_PLUGINS
    if (ops == NULL) {
        const struct na_class_ops *na_plugin_dynamic[NA_PLUGIN_MAX] = {NULL};
        int i;

        NA_CHECK_SUBSYS_ERROR(cls, na_plugin_dynamic_g == NULL, error, ret,
            NA_NOENTRY, "No dynamic plugins were found");

        for (i = 0, ops = na_plugin_dynamic_g[0].ops;
             i < NA_PLUGIN_MAX && ops != NULL;
             i++, ops = na_plugin_dynamic_g[i].ops)
            na_plugin_dynamic[i] = ops;

        /* Check list of dynamic plugins */
        ret = na_plugin_check_protocol(
            (const struct na_class_ops *const *) na_plugin_dynamic, class_name,
            na_info->protocol_name, &ops);
        NA_CHECK_SUBSYS_NA_ERROR(
            cls, error, ret, "Could not check dynamic plugins");
#endif

        NA_CHECK_SUBSYS_ERROR(fatal, ops == NULL, error, ret, NA_PROTONOSUPPORT,
            "No suitable plugin found that matches %s", info_string);
#ifdef NA_HAS_DYNAMIC_PLUGINS
    }
#endif

    na_private_class->na_class.protocol_name = strdup(na_info->protocol_name);
    NA_CHECK_SUBSYS_ERROR(cls, na_private_class->na_class.protocol_name == NULL,
        error, ret, NA_NOMEM, "Could not duplicate protocol name");

    na_private_class->na_class.ops = ops;
    NA_CHECK_SUBSYS_ERROR(cls, na_private_class->na_class.ops == NULL, error,
        ret, NA_INVALID_ARG, "NULL NA class ops");

    NA_CHECK_SUBSYS_ERROR(cls,
        na_private_class->na_class.ops->initialize == NULL, error, ret,
        NA_OPNOTSUPPORTED, "initialize plugin callback is not defined");

    ret = na_private_class->na_class.ops->initialize(
        &na_private_class->na_class, na_info, listen);
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not initialize plugin");

    na_private_class->na_class.listen = listen;

    free(class_name);
    na_info_free(na_info);

    return (na_class_t *) na_private_class;

error:
    free(class_name);
    na_info_free(na_info);
    if (na_private_class) {
        free(na_private_class->na_class.protocol_name);
        free(na_private_class);
    }
    return NULL;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Finalize(na_class_t *na_class)
{
    struct na_private_class *na_private_class =
        (struct na_private_class *) na_class;
    na_return_t ret;

    if (na_private_class == NULL)
        return NA_SUCCESS;

    NA_CHECK_SUBSYS_ERROR(cls,
        na_class->ops == NULL || na_class->ops->finalize == NULL, error, ret,
        NA_OPNOTSUPPORTED, "finalize plugin callback is not defined");

    ret = na_class->ops->finalize(&na_private_class->na_class);
    NA_CHECK_SUBSYS_NA_ERROR(cls, error, ret, "Could not finalize plugin");

    free(na_private_class->na_class.protocol_name);
    free(na_private_class);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
void
NA_Cleanup(void)
{
    const struct na_class_ops *ops = NULL;
    int i;

    for (i = 0, ops = na_plugin_static_g[0]; ops != NULL;
         i++, ops = na_plugin_static_g[i])
        if (ops->cleanup)
            ops->cleanup();
}

/*---------------------------------------------------------------------------*/
bool
NA_Has_opt_feature(na_class_t *na_class, unsigned long flags)
{
    if (na_class && na_class->ops && na_class->ops->has_opt_feature)
        return na_class->ops->has_opt_feature(na_class, flags);
    else
        return false;
}

/*---------------------------------------------------------------------------*/
void
NA_Set_log_level(const char *level)
{
    hg_log_set_subsys_level(NA_SUBSYS_NAME_STRING, hg_log_name_to_level(level));
}

/*---------------------------------------------------------------------------*/
na_context_t *
NA_Context_create(na_class_t *na_class)
{
    return NA_Context_create_id(na_class, 0);
}

/*---------------------------------------------------------------------------*/
na_context_t *
NA_Context_create_id(na_class_t *na_class, uint8_t id)
{
    struct na_private_context *na_private_context = NULL;
#ifdef NA_HAS_MULTI_PROGRESS
    struct na_progress_multi *progress_multi = NULL;
    bool mutex_init = false, cond_init = false;
#endif
    struct na_completion_queue *backfill_queue = NULL;
    bool lock_init = false;
    na_return_t ret;
    int rc;

    NA_CHECK_SUBSYS_ERROR(
        ctx, na_class == NULL, error, ret, NA_INVALID_ARG, "NULL NA class");

    na_private_context =
        (struct na_private_context *) calloc(1, sizeof(*na_private_context));
    NA_CHECK_SUBSYS_ERROR(ctx, na_private_context == NULL, error, ret, NA_NOMEM,
        "Could not allocate context");
    na_private_context->na_class = na_class;

#ifdef NA_HAS_MULTI_PROGRESS
    /* Initialize multi-progress lock */
    progress_multi = &na_private_context->progress_multi;
    hg_atomic_init32(&progress_multi->count, 0);
    rc = hg_thread_mutex_init(&progress_multi->mutex);
    NA_CHECK_SUBSYS_ERROR_NORET(
        ctx, rc != HG_UTIL_SUCCESS, error, "hg_thread_mutex_init() failed");
    mutex_init = true;

    rc = hg_thread_cond_init(&progress_multi->cond);
    NA_CHECK_SUBSYS_ERROR_NORET(
        ctx, rc != HG_UTIL_SUCCESS, error, "hg_thread_cond_init() failed");
    cond_init = true;
#endif

    /* Initialize backfill queue */
    backfill_queue = &na_private_context->backfill_queue;
    STAILQ_INIT(&backfill_queue->queue);
    hg_atomic_init32(&backfill_queue->count, 0);
    rc = hg_thread_spin_init(&backfill_queue->lock);
    NA_CHECK_SUBSYS_ERROR_NORET(
        ctx, rc != HG_UTIL_SUCCESS, error, "hg_thread_spin_init() failed");
    lock_init = true;

    /* Initialize completion queue */
    na_private_context->completion_queue =
        hg_atomic_queue_alloc(NA_ATOMIC_QUEUE_SIZE);
    NA_CHECK_SUBSYS_ERROR(ctx, na_private_context->completion_queue == NULL,
        error, ret, NA_NOMEM, "Could not allocate queue");

    /* Initialize plugin context */
    if (na_class->ops && na_class->ops->context_create) {
        ret = na_class->ops->context_create(
            na_class, &na_private_context->context.plugin_context, id);
        NA_CHECK_SUBSYS_NA_ERROR(
            ctx, error, ret, "Could not create plugin context");
    }

    return (na_context_t *) na_private_context;

error:
    if (na_private_context) {
#ifdef NA_HAS_MULTI_PROGRESS
        if (mutex_init)
            (void) hg_thread_mutex_destroy(&progress_multi->mutex);
        if (cond_init)
            (void) hg_thread_cond_destroy(&progress_multi->cond);
#endif
        if (lock_init)
            (void) hg_thread_spin_destroy(&backfill_queue->lock);
        hg_atomic_queue_free(na_private_context->completion_queue);
        free(na_private_context);
    }
    return NULL;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Context_destroy(na_class_t *na_class, na_context_t *context)
{
    struct na_private_context *na_private_context =
        (struct na_private_context *) context;
#ifdef NA_HAS_MULTI_PROGRESS
    struct na_progress_multi *progress_multi = NULL;
#endif
    struct na_completion_queue *backfill_queue = NULL;
    bool empty;
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(
        ctx, na_class == NULL, error, ret, NA_INVALID_ARG, "NULL NA class");

    if (na_private_context == NULL)
        return NA_SUCCESS;

#ifdef NA_HAS_MULTI_PROGRESS
    /* Check that we are no longer progressing */
    progress_multi = &na_private_context->progress_multi;
    NA_CHECK_SUBSYS_ERROR(ctx, hg_atomic_get32(&progress_multi->count) > 0,
        error, ret, NA_BUSY, "Still progressing on context");
#endif

    /* Check that backfill completion queue is empty now */
    backfill_queue = &na_private_context->backfill_queue;
    hg_thread_spin_lock(&backfill_queue->lock);
    empty = STAILQ_EMPTY(&backfill_queue->queue);
    hg_thread_spin_unlock(&backfill_queue->lock);
    NA_CHECK_SUBSYS_ERROR(ctx, empty == false, error, ret, NA_BUSY,
        "Completion queue should be empty");

    /* Check that completion queue is empty now */
    empty = hg_atomic_queue_is_empty(na_private_context->completion_queue);
    NA_CHECK_SUBSYS_ERROR(ctx, empty == false, error, ret, NA_BUSY,
        "Completion queue should be empty (%u entries remaining)",
        hg_atomic_queue_count(na_private_context->completion_queue));

    /* Destroy NA plugin context */
    if (na_class->ops && na_class->ops->context_destroy) {
        ret = na_class->ops->context_destroy(
            na_class, na_private_context->context.plugin_context);
        NA_CHECK_SUBSYS_NA_ERROR(
            ctx, error, ret, "Could not destroy plugin context");
    }

    hg_atomic_queue_free(na_private_context->completion_queue);
    (void) hg_thread_spin_destroy(&backfill_queue->lock);
#ifdef NA_HAS_MULTI_PROGRESS
    (void) hg_thread_mutex_destroy(&progress_multi->mutex);
    (void) hg_thread_cond_destroy(&progress_multi->cond);
#endif
    free(na_private_context);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
unsigned int
NA_Context_get_completion_count(const na_context_t *context)
{
    const struct na_private_context *na_private_context =
        (const struct na_private_context *) context;

    NA_CHECK_SUBSYS_ERROR_NORET(ctx, context == NULL, error, "NULL context");

    return hg_atomic_queue_count(na_private_context->completion_queue) +
           (unsigned int) hg_atomic_get32(
               &na_private_context->backfill_queue.count);

error:
    return 0;
}

/*---------------------------------------------------------------------------*/
na_op_id_t *
NA_Op_create(na_class_t *na_class, unsigned long flags)
{
    na_op_id_t *ret;

    NA_CHECK_SUBSYS_ERROR_NORET(op, na_class == NULL, error, "NULL NA class");
    NA_CHECK_SUBSYS_ERROR_NORET(op,
        na_class->ops == NULL || na_class->ops->op_create == NULL, error,
        "op_create plugin callback is not defined");

    ret = na_class->ops->op_create(na_class, flags);
    NA_CHECK_SUBSYS_ERROR_NORET(
        op, ret == NULL, error, "Could not create OP ID");

    NA_LOG_SUBSYS_DEBUG(op, "Created new OP ID (%p)", (void *) ret);

    return ret;

error:
    return NULL;
}

/*---------------------------------------------------------------------------*/
void
NA_Op_destroy(na_class_t *na_class, na_op_id_t *op_id)
{
    NA_CHECK_SUBSYS_ERROR_NORET(op, na_class == NULL, error, "NULL NA class");

    if (op_id == NULL)
        return;

    NA_CHECK_SUBSYS_ERROR_NORET(op,
        na_class->ops == NULL || na_class->ops->op_destroy == NULL, error,
        "op_destroy plugin callback is not defined");

    NA_LOG_SUBSYS_DEBUG(op, "Destroying OP ID (%p)", (void *) op_id);

    na_class->ops->op_destroy(na_class, op_id);

error:
    return;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Addr_lookup(na_class_t *na_class, const char *name, na_addr_t **addr_p)
{
    const char *short_name = NULL;
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(
        addr, na_class == NULL, error, ret, NA_INVALID_ARG, "NULL NA class");
    NA_CHECK_SUBSYS_ERROR(
        addr, name == NULL, error, ret, NA_INVALID_ARG, "Lookup name is NULL");
    NA_CHECK_SUBSYS_ERROR(addr, addr_p == NULL, error, ret, NA_INVALID_ARG,
        "NULL pointer to NA addr");

    NA_CHECK_SUBSYS_ERROR(addr,
        na_class->ops == NULL || na_class->ops->addr_lookup == NULL, error, ret,
        NA_PROTOCOL_ERROR, "addr_lookup2 plugin callback is not defined");

    /* If NA class name was specified, we can remove the name here:
     * ie. bmi+tcp://hostname:port -> tcp://hostname:port */
    short_name = strstr(name, NA_CLASS_DELIMITER);
    short_name =
        (short_name == NULL) ? name : short_name + NA_CLASS_DELIMITER_LEN;

    NA_LOG_SUBSYS_DEBUG(addr, "Looking up addr %s", short_name);

    ret = na_class->ops->addr_lookup(na_class, short_name, addr_p);
    NA_CHECK_SUBSYS_NA_ERROR(
        addr, error, ret, "Could not lookup address for %s", short_name);

    NA_LOG_SUBSYS_DEBUG(addr, "Created new address (%p)", (void *) *addr_p);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
void
NA_Addr_free(na_class_t *na_class, na_addr_t *addr)
{
    NA_CHECK_SUBSYS_ERROR_NORET(addr, na_class == NULL, error, "NULL NA class");

    if (addr == NULL)
        return;

    NA_CHECK_SUBSYS_ERROR_NORET(addr,
        na_class->ops == NULL || na_class->ops->addr_free == NULL, error,
        "addr_free plugin callback is not defined");

    NA_LOG_SUBSYS_DEBUG(addr, "Freeing address (%p)", (void *) addr);

    na_class->ops->addr_free(na_class, addr);

error:
    return;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Addr_set_remove(na_class_t *na_class, na_addr_t *addr)
{
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(
        addr, na_class == NULL, error, ret, NA_INVALID_ARG, "NULL NA class");
    NA_CHECK_SUBSYS_ERROR(addr, addr == NULL, error, ret, NA_INVALID_ARG,
        "NULL pointer to NA addr");

    if (na_class->ops && na_class->ops->addr_set_remove) {
        ret = na_class->ops->addr_set_remove(na_class, addr);
        NA_CHECK_SUBSYS_NA_ERROR(addr, error, ret,
            "Could not set remove for address (%p)", (void *) addr);
    }

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Addr_self(na_class_t *na_class, na_addr_t **addr_p)
{
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(
        addr, na_class == NULL, error, ret, NA_INVALID_ARG, "NULL NA class");
    NA_CHECK_SUBSYS_ERROR(addr, addr_p == NULL, error, ret, NA_INVALID_ARG,
        "NULL pointer to NA addr");

    NA_CHECK_SUBSYS_ERROR(addr,
        na_class->ops == NULL || na_class->ops->addr_self == NULL, error, ret,
        NA_OPNOTSUPPORTED, "addr_self plugin callback is not defined");

    ret = na_class->ops->addr_self(na_class, addr_p);
    NA_CHECK_SUBSYS_NA_ERROR(addr, error, ret, "Could not get self address");

    NA_LOG_SUBSYS_DEBUG(
        addr, "Created new self address (%p)", (void *) *addr_p);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Addr_dup(na_class_t *na_class, na_addr_t *addr, na_addr_t **new_addr_p)
{
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(
        addr, na_class == NULL, error, ret, NA_INVALID_ARG, "NULL NA class");
    NA_CHECK_SUBSYS_ERROR(
        addr, addr == NULL, error, ret, NA_INVALID_ARG, "NULL addr");
    NA_CHECK_SUBSYS_ERROR(addr, new_addr_p == NULL, error, ret, NA_INVALID_ARG,
        "NULL pointer to NA addr");

    NA_CHECK_SUBSYS_ERROR(addr,
        na_class->ops == NULL || na_class->ops->addr_dup == NULL, error, ret,
        NA_OPNOTSUPPORTED, "addr_dup plugin callback is not defined");

    ret = na_class->ops->addr_dup(na_class, addr, new_addr_p);
    NA_CHECK_SUBSYS_NA_ERROR(
        addr, error, ret, "Could not dup address (%p)", (void *) addr);

    NA_LOG_SUBSYS_DEBUG(addr, "Dup'ed address (%p) to (%p)", (void *) addr,
        (void *) *new_addr_p);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
bool
NA_Addr_cmp(na_class_t *na_class, na_addr_t *addr1, na_addr_t *addr2)
{
    bool ret = false;

    NA_CHECK_SUBSYS_ERROR_NORET(addr, na_class == NULL, done, "NULL NA class");

    if (addr1 == NULL && addr2 == NULL)
        NA_GOTO_DONE(done, ret, true);

    if (addr1 == NULL || addr2 == NULL)
        NA_GOTO_DONE(done, ret, false);

    NA_CHECK_SUBSYS_ERROR_NORET(addr,
        na_class->ops == NULL || na_class->ops->addr_cmp == NULL, done,
        "addr_cmp plugin callback is not defined");

    ret = na_class->ops->addr_cmp(na_class, addr1, addr2);

    NA_LOG_SUBSYS_DEBUG(addr, "Compared addresses (%p) and (%p), result: %d",
        (void *) addr1, (void *) addr2, ret);

done:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Addr_to_string(
    na_class_t *na_class, char *buf, size_t *buf_size, na_addr_t *addr)
{
    char *buf_ptr = buf;
    size_t buf_size_used = 0, plugin_buf_size = 0;
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(
        addr, na_class == NULL, error, ret, NA_INVALID_ARG, "NULL NA class");
    /* buf can be NULL */
    NA_CHECK_SUBSYS_ERROR(
        addr, buf_size == 0, error, ret, NA_INVALID_ARG, "NULL buffer size");
    NA_CHECK_SUBSYS_ERROR(
        addr, addr == NULL, error, ret, NA_INVALID_ARG, "NULL addr");

    NA_CHECK_SUBSYS_ERROR(addr,
        na_class->ops == NULL || na_class->ops->addr_to_string == NULL, error,
        ret, NA_OPNOTSUPPORTED,
        "addr_to_string plugin callback is not defined");

    /* Automatically prepend string by plugin name with class delimiter,
     * except for MPI plugin (special case, because of generated string) */
    if (strcmp(na_class->ops->class_name, "mpi") == 0) {
        buf_size_used = 0;
        plugin_buf_size = *buf_size;
    } else {
        buf_size_used =
            strlen(na_class->ops->class_name) + NA_CLASS_DELIMITER_LEN;
        if (buf_ptr) {
            NA_CHECK_SUBSYS_ERROR(addr, buf_size_used >= *buf_size, error, ret,
                NA_OVERFLOW, "Buffer size too small to copy addr");
            strcpy(buf_ptr, na_class->ops->class_name);
            strcat(buf_ptr, NA_CLASS_DELIMITER);
            buf_ptr += buf_size_used;
            plugin_buf_size = *buf_size - buf_size_used;
        } else
            plugin_buf_size = 0;
    }

    ret = na_class->ops->addr_to_string(
        na_class, buf_ptr, &plugin_buf_size, addr);
    NA_CHECK_SUBSYS_NA_ERROR(addr, error, ret,
        "Could not generate string from addr (%p)", (void *) addr);

    *buf_size = buf_size_used + plugin_buf_size;

    NA_LOG_SUBSYS_DEBUG(addr,
        "Generated string (%s) from address (%p), buf_size=%zu", buf_ptr,
        (void *) addr, *buf_size);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Addr_serialize(
    na_class_t *na_class, void *buf, size_t buf_size, na_addr_t *addr)
{
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(
        addr, na_class == NULL, error, ret, NA_INVALID_ARG, "NULL NA class");
    NA_CHECK_SUBSYS_ERROR(
        addr, buf == NULL, error, ret, NA_INVALID_ARG, "NULL buffer");
    NA_CHECK_SUBSYS_ERROR(
        addr, buf_size == 0, error, ret, NA_INVALID_ARG, "NULL buffer size");
    NA_CHECK_SUBSYS_ERROR(
        addr, addr == NULL, error, ret, NA_INVALID_ARG, "NULL addr");

    NA_CHECK_SUBSYS_ERROR(addr,
        na_class->ops == NULL || na_class->ops->addr_serialize == NULL, error,
        ret, NA_OPNOTSUPPORTED,
        "addr_serialize plugin callback is not defined");

    NA_LOG_SUBSYS_DEBUG(addr, "Serializing address (%p)", (void *) addr);

    ret = na_class->ops->addr_serialize(na_class, buf, buf_size, addr);
    NA_CHECK_SUBSYS_NA_ERROR(
        addr, error, ret, "Could not serialize addr (%p)", (void *) addr);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Addr_deserialize(
    na_class_t *na_class, na_addr_t **addr_p, const void *buf, size_t buf_size)
{
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(
        addr, na_class == NULL, error, ret, NA_INVALID_ARG, "NULL NA class");
    NA_CHECK_SUBSYS_ERROR(addr, addr_p == NULL, error, ret, NA_INVALID_ARG,
        "NULL pointer to addr");
    NA_CHECK_SUBSYS_ERROR(
        addr, buf == NULL, error, ret, NA_INVALID_ARG, "NULL buffer");
    NA_CHECK_SUBSYS_ERROR(
        addr, buf_size == 0, error, ret, NA_INVALID_ARG, "NULL buffer size");

    NA_CHECK_SUBSYS_ERROR(addr,
        na_class->ops == NULL || na_class->ops->addr_deserialize == NULL, error,
        ret, NA_OPNOTSUPPORTED,
        "addr_deserialize plugin callback is not defined");

    ret = na_class->ops->addr_deserialize(na_class, addr_p, buf, buf_size);
    NA_CHECK_SUBSYS_NA_ERROR(addr, error, ret,
        "Could not deserialize addr from buffer (%p, %zu)", buf, buf_size);

    NA_LOG_SUBSYS_DEBUG(
        addr, "Deserialized into new address (%p)", (void *) *addr_p);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
void *
NA_Msg_buf_alloc(na_class_t *na_class, size_t buf_size, unsigned long flags,
    void **plugin_data_p)
{
    void *ret = NULL;

    NA_CHECK_SUBSYS_ERROR_NORET(msg, na_class == NULL, error, "NULL NA class");
    NA_CHECK_SUBSYS_ERROR_NORET(msg, buf_size == 0, error, "NULL buffer size");
    NA_CHECK_SUBSYS_ERROR_NORET(
        msg, plugin_data_p == NULL, error, "NULL pointer to plugin data");

    if (na_class->ops && na_class->ops->msg_buf_alloc) {
        ret = na_class->ops->msg_buf_alloc(
            na_class, buf_size, flags, plugin_data_p);
        NA_CHECK_SUBSYS_ERROR_NORET(msg, ret == NULL, error,
            "Could not allocate buffer of size %zu", buf_size);
    } else {
        size_t page_size = (size_t) hg_mem_get_page_size();

        ret = hg_mem_aligned_alloc(page_size, buf_size);
        NA_CHECK_SUBSYS_ERROR_NORET(msg, ret == NULL, error,
            "Could not allocate buffer of size %zu", buf_size);
        memset(ret, 0, buf_size);
        *plugin_data_p = (void *) 1; /* Sanity check on free */
    }

    NA_LOG_SUBSYS_DEBUG(msg,
        "Allocated msg buffer (%p), size (%zu bytes), plugin data (%p)", ret,
        buf_size, *plugin_data_p);

    return ret;

error:
    return NULL;
}

/*---------------------------------------------------------------------------*/
void
NA_Msg_buf_free(na_class_t *na_class, void *buf, void *plugin_data)
{
    NA_CHECK_SUBSYS_ERROR_NORET(msg, na_class == NULL, error, "NULL NA class");

    if (buf == NULL)
        return;

    NA_LOG_SUBSYS_DEBUG(
        msg, "Freeing msg buffer (%p), plugin data (%p)", buf, plugin_data);

    if (na_class->ops && na_class->ops->msg_buf_free) {
        na_class->ops->msg_buf_free(na_class, buf, plugin_data);
    } else {
        NA_CHECK_SUBSYS_WARNING(
            msg, plugin_data != (void *) 1, "Invalid plugin data value");
        hg_mem_aligned_free(buf);
    }

error:
    return;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Msg_init_unexpected(na_class_t *na_class, void *buf, size_t buf_size)
{
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(
        msg, na_class == NULL, error, ret, NA_INVALID_ARG, "NULL NA class");
    NA_CHECK_SUBSYS_ERROR(
        msg, buf == NULL, error, ret, NA_INVALID_ARG, "NULL buffer");
    NA_CHECK_SUBSYS_ERROR(
        msg, buf_size == 0, error, ret, NA_INVALID_ARG, "NULL buffer size");

    /* Optional, silently returns */
    if (na_class->ops && na_class->ops->msg_init_unexpected) {
        ret = na_class->ops->msg_init_unexpected(na_class, buf, buf_size);
        NA_CHECK_SUBSYS_NA_ERROR(
            msg, error, ret, "Could not init unexpected buffer (%p)", buf);

        NA_LOG_SUBSYS_DEBUG(
            msg, "Init unexpected buf (%p), size (%zu)", buf, buf_size);
    }

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Msg_init_expected(na_class_t *na_class, void *buf, size_t buf_size)
{
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(
        msg, na_class == NULL, error, ret, NA_INVALID_ARG, "NULL NA class");
    NA_CHECK_SUBSYS_ERROR(
        msg, buf == NULL, error, ret, NA_INVALID_ARG, "NULL buffer");
    NA_CHECK_SUBSYS_ERROR(
        msg, buf_size == 0, error, ret, NA_INVALID_ARG, "NULL buffer size");

    /* Optional, silently returns */
    if (na_class->ops && na_class->ops->msg_init_expected) {
        ret = na_class->ops->msg_init_expected(na_class, buf, buf_size);
        NA_CHECK_SUBSYS_NA_ERROR(
            msg, error, ret, "Could not init expected buffer (%p)", buf);

        NA_LOG_SUBSYS_DEBUG(
            msg, "Init expected buf (%p), size (%zu)", buf, buf_size);
    }

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Mem_handle_create(na_class_t *na_class, void *buf, size_t buf_size,
    unsigned long flags, na_mem_handle_t **mem_handle_p)
{
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(
        mem, na_class == NULL, error, ret, NA_INVALID_ARG, "NULL NA class");
    NA_CHECK_SUBSYS_ERROR(
        mem, buf == NULL, error, ret, NA_INVALID_ARG, "NULL buffer");
    NA_CHECK_SUBSYS_ERROR(
        mem, buf_size == 0, error, ret, NA_INVALID_ARG, "NULL buffer size");

    NA_CHECK_SUBSYS_ERROR(mem,
        na_class->ops == NULL || na_class->ops->mem_handle_create == NULL,
        error, ret, NA_OPNOTSUPPORTED,
        "mem_handle_create plugin callback is not defined");

    ret = na_class->ops->mem_handle_create(
        na_class, buf, buf_size, flags, mem_handle_p);
    NA_CHECK_SUBSYS_NA_ERROR(mem, error, ret, "Could not create memory handle");

    NA_LOG_SUBSYS_DEBUG(mem,
        "Created new mem handle (%p), buf (%p), buf_size (%zu), flags "
        "(%lu)",
        (void *) *mem_handle_p, buf, buf_size, flags);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Mem_handle_create_segments(na_class_t *na_class, struct na_segment *segments,
    size_t segment_count, unsigned long flags, na_mem_handle_t **mem_handle_p)
{
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(
        mem, na_class == NULL, error, ret, NA_INVALID_ARG, "NULL NA class");
    NA_CHECK_SUBSYS_ERROR(mem, segments == NULL, error, ret, NA_INVALID_ARG,
        "NULL pointer to segments");
    NA_CHECK_SUBSYS_ERROR(mem, segment_count == 0, error, ret, NA_INVALID_ARG,
        "NULL segment count");

    NA_CHECK_SUBSYS_ERROR(mem,
        na_class->ops == NULL ||
            na_class->ops->mem_handle_create_segments == NULL,
        error, ret, NA_OPNOTSUPPORTED,
        "mem_handle_create_segments plugin callback is not defined");

    ret = na_class->ops->mem_handle_create_segments(
        na_class, segments, segment_count, flags, mem_handle_p);
    NA_CHECK_SUBSYS_NA_ERROR(mem, error, ret, "Could not create memory handle");

    NA_LOG_SUBSYS_DEBUG(mem,
        "Created new mem handle (%p) with %zu segments, flags (%lu)",
        (void *) *mem_handle_p, segment_count, flags);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
void
NA_Mem_handle_free(na_class_t *na_class, na_mem_handle_t *mem_handle)
{
    NA_CHECK_SUBSYS_ERROR_NORET(mem, na_class == NULL, error, "NULL NA class");

    if (mem_handle == NULL)
        return;

    NA_CHECK_SUBSYS_ERROR_NORET(mem,
        na_class->ops == NULL || na_class->ops->mem_handle_free == NULL, error,
        "mem_handle_free plugin callback is not defined");

    NA_LOG_SUBSYS_DEBUG(mem, "Freeing mem handle (%p)", (void *) mem_handle);

    na_class->ops->mem_handle_free(na_class, mem_handle);

error:
    return;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Mem_register(na_class_t *na_class, na_mem_handle_t *mem_handle,
    enum na_mem_type mem_type, uint64_t device)
{
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(
        mem, na_class == NULL, error, ret, NA_INVALID_ARG, "NULL NA class");
    NA_CHECK_SUBSYS_ERROR(mem, mem_handle == NULL, error, ret, NA_INVALID_ARG,
        "NULL memory handle");

    /* Optional, silently returns */
    if (na_class->ops && na_class->ops->mem_register) {
        ret =
            na_class->ops->mem_register(na_class, mem_handle, mem_type, device);
        NA_CHECK_SUBSYS_NA_ERROR(mem, error, ret,
            "Could not register mem handle (%p)", (void *) mem_handle);

        NA_LOG_SUBSYS_DEBUG(
            mem, "Registered mem handle (%p)", (void *) mem_handle);
    }

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Mem_deregister(na_class_t *na_class, na_mem_handle_t *mem_handle)
{
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(
        mem, na_class == NULL, error, ret, NA_INVALID_ARG, "NULL NA class");
    NA_CHECK_SUBSYS_ERROR(mem, mem_handle == NULL, error, ret, NA_INVALID_ARG,
        "NULL memory handle");

    /* Optional, silently returns */
    if (na_class->ops && na_class->ops->mem_deregister) {
        NA_LOG_SUBSYS_DEBUG(
            mem, "Deregistering mem handle (%p)", (void *) mem_handle);

        ret = na_class->ops->mem_deregister(na_class, mem_handle);
        NA_CHECK_SUBSYS_NA_ERROR(mem, error, ret,
            "Could not deregister mem handle (%p)", (void *) mem_handle);
    }

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Mem_handle_serialize(na_class_t *na_class, void *buf, size_t buf_size,
    na_mem_handle_t *mem_handle)
{
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(
        mem, na_class == NULL, error, ret, NA_INVALID_ARG, "NULL NA class");
    NA_CHECK_SUBSYS_ERROR(
        mem, buf == NULL, error, ret, NA_INVALID_ARG, "NULL buffer");
    NA_CHECK_SUBSYS_ERROR(
        mem, buf_size == 0, error, ret, NA_INVALID_ARG, "NULL buffer size");
    NA_CHECK_SUBSYS_ERROR(mem, mem_handle == NULL, error, ret, NA_INVALID_ARG,
        "NULL memory handle");

    NA_CHECK_SUBSYS_ERROR(mem,
        na_class->ops == NULL || na_class->ops->mem_handle_serialize == NULL,
        error, ret, NA_OPNOTSUPPORTED,
        "mem_handle_serialize plugin callback is not defined");

    NA_LOG_SUBSYS_DEBUG(
        mem, "Serializing mem handle (%p)", (void *) mem_handle);

    ret = na_class->ops->mem_handle_serialize(
        na_class, buf, buf_size, mem_handle);
    NA_CHECK_SUBSYS_NA_ERROR(mem, error, ret,
        "Could not serialize mem handle (%p)", (void *) mem_handle);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Mem_handle_deserialize(na_class_t *na_class, na_mem_handle_t **mem_handle,
    const void *buf, size_t buf_size)
{
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(
        mem, na_class == NULL, error, ret, NA_INVALID_ARG, "NULL NA class");
    NA_CHECK_SUBSYS_ERROR(mem, mem_handle == NULL, error, ret, NA_INVALID_ARG,
        "NULL pointer to memory handle");
    NA_CHECK_SUBSYS_ERROR(
        mem, buf == NULL, error, ret, NA_INVALID_ARG, "NULL buffer");
    NA_CHECK_SUBSYS_ERROR(
        mem, buf_size == 0, error, ret, NA_INVALID_ARG, "NULL buffer size");

    NA_CHECK_SUBSYS_ERROR(mem,
        na_class->ops == NULL || na_class->ops->mem_handle_deserialize == NULL,
        error, ret, NA_OPNOTSUPPORTED,
        "mem_handle_deserialize plugin callback is not defined");

    ret = na_class->ops->mem_handle_deserialize(
        na_class, mem_handle, buf, buf_size);
    NA_CHECK_SUBSYS_NA_ERROR(mem, error, ret,
        "Could not deserialize mem handle from buffer (%p, %zu)", buf,
        buf_size);

    NA_LOG_SUBSYS_DEBUG(
        mem, "Deserialized into mem handle (%p)", (void *) *mem_handle);

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Poll_wait(na_class_t *na_class, na_context_t *context,
    unsigned int timeout_ms, unsigned int *count_p)
{
    unsigned int completion_count = NA_Context_get_completion_count(context);
    unsigned int wait_timeout = timeout_ms;
    na_return_t ret;

    if (completion_count > 0)
        wait_timeout = 0;

    if (na_class->ops->poll_wait)
        ret = na_class->ops->poll_wait(
            na_class, context, wait_timeout, NULL /* unused */);
    else
        ret = na_poll_busy_wait(na_class, context, wait_timeout);

    if ((ret == NA_TIMEOUT && completion_count > 0) || (ret == NA_SUCCESS)) {
        if (count_p != NULL)
            *count_p = NA_Context_get_completion_count(context);
        return NA_SUCCESS;
    } else
        return ret;
}

/*---------------------------------------------------------------------------*/
#ifdef NA_HAS_MULTI_PROGRESS
na_return_t
NA_Progress(
    na_class_t *na_class, na_context_t *context, unsigned int timeout_ms)
{
    struct na_private_context *na_private_context =
        (struct na_private_context *) context;
    struct na_progress_multi *progress_multi = NULL;
    double remaining =
        timeout_ms / 1000.0; /* Convert timeout in ms into seconds */
    int32_t old, num;
    na_return_t ret = NA_TIMEOUT;

    NA_CHECK_SUBSYS_ERROR(
        poll, na_class == NULL, done, ret, NA_INVALID_ARG, "NULL NA class");
    NA_CHECK_SUBSYS_ERROR(poll, na_private_context == NULL, done, ret,
        NA_INVALID_ARG, "NULL context");
    progress_multi = &na_private_context->progress_multi;

    NA_LOG_SUBSYS_DEBUG(poll_loop,
        "Entering progress on context (%p) for %u ms", (void *) context,
        timeout_ms);

    hg_atomic_incr32(&progress_multi->count);
    for (;;) {
        hg_time_t t1, t2;

        old = hg_atomic_get32(&progress_multi->count) &
              (int32_t) ~NA_PROGRESS_LOCK;
        num = old | (int32_t) NA_PROGRESS_LOCK;
        if (hg_atomic_cas32(&progress_multi->count, old, num))
            break; /* No other thread is progressing */

        /* Timeout is 0 so leave */
        if (remaining <= 0) {
            hg_atomic_decr32(&progress_multi->count);
            goto done;
        }

        hg_time_get_current_ms(&t1);

        /* Prevent multiple threads from concurrently calling progress on
         * the same context */
        hg_thread_mutex_lock(&progress_multi->mutex);

        num = hg_atomic_get32(&progress_multi->count);
        /* Do not need to enter condition if lock is already released */
        if (((num & (int32_t) NA_PROGRESS_LOCK) != 0) &&
            (hg_thread_cond_timedwait(&progress_multi->cond,
                 &progress_multi->mutex,
                 (unsigned int) (remaining * 1000.0)) != HG_UTIL_SUCCESS)) {
            /* Timeout occurred so leave */
            hg_atomic_decr32(&progress_multi->count);
            hg_thread_mutex_unlock(&progress_multi->mutex);
            goto done;
        }

        hg_thread_mutex_unlock(&progress_multi->mutex);

        hg_time_get_current_ms(&t2);
        remaining -= hg_time_diff(t2, t1);
        /* Give a chance to call progress with timeout of 0 */
        if (remaining < 0)
            remaining = 0;
    }

    ret = NA_Poll_wait(
        na_class, context, (unsigned int) (remaining * 1000.0), NULL);

    do {
        old = hg_atomic_get32(&progress_multi->count);
        num = (old - 1) ^ (int32_t) NA_PROGRESS_LOCK;
    } while (!hg_atomic_cas32(&progress_multi->count, old, num));

    if (num > 0) {
        /* If there is another processes entered in progress, signal it */
        hg_thread_mutex_lock(&progress_multi->mutex);
        hg_thread_cond_signal(&progress_multi->cond);
        hg_thread_mutex_unlock(&progress_multi->mutex);
    }

done:
    return ret;
}
#else
na_return_t
NA_Progress(
    na_class_t *na_class, na_context_t *context, unsigned int timeout_ms)
{
    return NA_Poll_wait(na_class, context, timeout_ms, NULL);
}
#endif

/*---------------------------------------------------------------------------*/
na_return_t
NA_Trigger(
    na_context_t *context, unsigned int max_count, unsigned int *actual_count)
{
    struct na_private_context *na_private_context =
        (struct na_private_context *) context;
    unsigned int count = 0;
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(
        op, context == NULL, error, ret, NA_INVALID_ARG, "NULL context");

    while (count < max_count) {
        struct na_cb_completion_data *completion_data_p = NULL;
        struct na_cb_completion_data completion_data;

        completion_data_p =
            hg_atomic_queue_pop_mc(na_private_context->completion_queue);
        if (completion_data_p == NULL) { /* Check backfill queue */
            struct na_completion_queue *backfill_queue =
                &na_private_context->backfill_queue;

            if (hg_atomic_get32(&backfill_queue->count)) {
                hg_thread_spin_lock(&backfill_queue->lock);
                if (hg_atomic_get32(&backfill_queue->count)) {
                    completion_data_p = STAILQ_FIRST(&backfill_queue->queue);
                    STAILQ_REMOVE_HEAD(&backfill_queue->queue, entry);
                    hg_atomic_decr32(&backfill_queue->count);
                }
                hg_thread_spin_unlock(&backfill_queue->lock);
                if (completion_data_p == NULL)
                    continue; /* Give another chance to grab it */
            } else
                break; /* Completion queues are empty */
        }

        /* Completion data should be valid */
        NA_CHECK_SUBSYS_ERROR(op, completion_data_p == NULL, error, ret,
            NA_INVALID_ARG, "NULL completion data");
        completion_data = *completion_data_p;

        /* Execute plugin callback (free resources etc) first since actual
         * callback will notify user that operation has completed.
         * NB. If the NA operation ID is reused by the plugin for another
         * operation we must be careful that resources are released BEFORE
         * that operation ID gets re-used.
         */
        if (completion_data.plugin_callback)
            completion_data.plugin_callback(
                completion_data.plugin_callback_args);

        /* Execute callback */
        if (completion_data.callback)
            completion_data.callback(&completion_data.callback_info);

        count++;
    }

    if (actual_count)
        *actual_count = count;

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
na_return_t
NA_Cancel(na_class_t *na_class, na_context_t *context, na_op_id_t *op_id)
{
    na_return_t ret;

    NA_CHECK_SUBSYS_ERROR(
        op, na_class == NULL, error, ret, NA_INVALID_ARG, "NULL NA class");
    NA_CHECK_SUBSYS_ERROR(
        op, context == NULL, error, ret, NA_INVALID_ARG, "NULL context");
    NA_CHECK_SUBSYS_ERROR(
        op, op_id == NULL, error, ret, NA_INVALID_ARG, "NULL operation ID");

    NA_CHECK_SUBSYS_ERROR(op,
        na_class->ops == NULL || na_class->ops->cancel == NULL, error, ret,
        NA_OPNOTSUPPORTED, "cancel plugin callback is not defined");

    NA_LOG_SUBSYS_DEBUG(op, "Canceling op ID (%p)", (void *) op_id);

    ret = na_class->ops->cancel(na_class, context, op_id);
    NA_CHECK_SUBSYS_NA_ERROR(
        op, error, ret, "Could not cancel op ID (%p)", (void *) op_id);

    return NA_SUCCESS;

error:
    return ret;
}

/*---------------------------------------------------------------------------*/
const char *
NA_Error_to_string(na_return_t errnum)
{
    return errnum < NA_RETURN_MAX ? na_return_name_g[errnum] : NULL;
}

/*---------------------------------------------------------------------------*/
const char *
na_cb_type_to_string(na_cb_type_t cb_type)
{
    return na_cb_type_name_g[cb_type];
}

/*---------------------------------------------------------------------------*/
struct na_protocol_info *
na_protocol_info_alloc(
    const char *class_name, const char *protocol_name, const char *device_name)
{
    struct na_protocol_info *entry;

    entry = (struct na_protocol_info *) calloc(1, sizeof(*entry));
    NA_CHECK_SUBSYS_ERROR_NORET(
        cls, entry == NULL, error, "Could not allocate entry");

    entry->class_name = strdup(class_name);
    NA_CHECK_SUBSYS_ERROR_NORET(
        cls, entry->class_name == NULL, error, "Could not dup class name");

    entry->protocol_name = strdup(protocol_name);
    NA_CHECK_SUBSYS_ERROR_NORET(cls, entry->protocol_name == NULL, error,
        "Could not dup protocol name");

    entry->device_name = strdup(device_name);
    NA_CHECK_SUBSYS_ERROR_NORET(
        cls, entry->device_name == NULL, error, "Could not dup device name");

    return entry;

error:
    if (entry != NULL)
        na_protocol_info_free(entry);

    return NULL;
}

/*---------------------------------------------------------------------------*/
void
na_protocol_info_free(struct na_protocol_info *entry)
{
    free(entry->class_name);
    free(entry->protocol_name);
    free(entry->device_name);
    free(entry);
}

/*---------------------------------------------------------------------------*/
void
na_cb_completion_add(
    na_context_t *context, struct na_cb_completion_data *na_cb_completion_data)
{
    struct na_private_context *na_private_context =
        (struct na_private_context *) context;
    struct na_completion_queue *backfill_queue =
        &na_private_context->backfill_queue;

    if (hg_atomic_queue_push(na_private_context->completion_queue,
            na_cb_completion_data) != HG_UTIL_SUCCESS) {
        NA_LOG_SUBSYS_WARNING(perf, "Atomic completion queue is full, pushing "
                                    "completion data to backfill queue");

        /* Queue is full */
        hg_thread_spin_lock(&backfill_queue->lock);
        STAILQ_INSERT_TAIL(
            &backfill_queue->queue, na_cb_completion_data, entry);
        hg_atomic_incr32(&backfill_queue->count);
        hg_thread_spin_unlock(&backfill_queue->lock);
    }
}
