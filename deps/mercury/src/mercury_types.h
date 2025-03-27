/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef MERCURY_TYPES_H
#define MERCURY_TYPES_H

#include "mercury_core_types.h"

/*************************************/
/* Public Type and Struct Definition */
/*************************************/

typedef struct hg_class hg_class_t;     /* Opaque HG class */
typedef struct hg_context hg_context_t; /* Opaque HG context */
typedef struct hg_addr *hg_addr_t;      /* Abstract HG address */
typedef struct hg_handle *hg_handle_t;  /* Abstract RPC handle */
typedef struct hg_bulk *hg_bulk_t;      /* Abstract bulk data handle */
typedef struct hg_proc *hg_proc_t;      /* Abstract serialization processor */
typedef struct hg_op_id *hg_op_id_t;    /* Abstract operation id */

/* HG info struct */
struct hg_info {
    hg_class_t *hg_class;  /* HG class */
    hg_context_t *context; /* HG context */
    hg_addr_t addr;        /* HG address at target/origin */
    hg_id_t id;            /* RPC ID */
    uint8_t context_id;    /* Context ID at target/origin */
};

/* Bulk attributes */
typedef enum hg_mem_type {
    HG_MEM_TYPE_HOST = NA_MEM_TYPE_HOST, /*!< Default system memory */
    HG_MEM_TYPE_CUDA = NA_MEM_TYPE_CUDA, /*!< NVIDIA CUDA memory */
    HG_MEM_TYPE_ROCM = NA_MEM_TYPE_ROCM, /*!< AMD ROCM memory */
    HG_MEM_TYPE_ZE = NA_MEM_TYPE_ZE,     /*!< Intel Level Zero memory */
    HG_MEM_TYPE_MAX = NA_MEM_TYPE_MAX,
    HG_MEM_TYPE_UNKNOWN = NA_MEM_TYPE_UNKNOWN
} hg_mem_type_t;

struct hg_bulk_attr {
    hg_mem_type_t mem_type; /*!< Memory type */
    uint64_t device;        /*!< Optional device ID */
};

/**
 * Bulk transfer operators.
 */
typedef enum hg_bulk_op {
    HG_BULK_PUSH, /*!< push data to origin */
    HG_BULK_PULL  /*!< pull data from origin */
} hg_bulk_op_t;

/* Callback info structs */
struct hg_cb_info_lookup {
    hg_addr_t addr; /* HG address */
};

struct hg_cb_info_forward {
    hg_handle_t handle; /* HG handle */
};

struct hg_cb_info_respond {
    hg_handle_t handle; /* HG handle */
};

struct hg_cb_info_bulk {
    hg_bulk_t origin_handle; /* HG Bulk origin handle */
    hg_bulk_t local_handle;  /* HG Bulk local handle */
    hg_bulk_op_t op;         /* Operation type */
    hg_size_t size;          /* Total size transferred */
};

struct hg_cb_info {
    union { /* Union of callback info structures */
        struct hg_cb_info_lookup lookup;
        struct hg_cb_info_forward forward;
        struct hg_cb_info_respond respond;
        struct hg_cb_info_bulk bulk;
    } info;
    void *arg;         /* User data */
    hg_cb_type_t type; /* Callback type */
    hg_return_t ret;   /* Return value */
};

/* RPC / HG callbacks */
typedef hg_return_t (*hg_rpc_cb_t)(hg_handle_t handle);
typedef hg_return_t (*hg_cb_t)(const struct hg_cb_info *callback_info);

/* Proc callback for serializing/deserializing parameters */
typedef hg_return_t (*hg_proc_cb_t)(hg_proc_t proc, void *data);

/*****************/
/* Public Macros */
/*****************/

/* Constant values */
#define HG_ADDR_NULL    ((hg_addr_t) 0)
#define HG_HANDLE_NULL  ((hg_handle_t) 0)
#define HG_BULK_NULL    ((hg_bulk_t) 0)
#define HG_PROC_NULL    ((hg_proc_t) 0)
#define HG_OP_ID_NULL   ((hg_op_id_t) 0)
#define HG_OP_ID_IGNORE ((hg_op_id_t *) 1)

#endif /* MERCURY_TYPES_H */
