/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: srv.proto */

#ifndef PROTOBUF_C_srv_2eproto__INCLUDED
#define PROTOBUF_C_srv_2eproto__INCLUDED

#include <protobuf-c/protobuf-c.h>

PROTOBUF_C__BEGIN_DECLS

#if PROTOBUF_C_VERSION_NUMBER < 1003000
# error This file was generated by a newer version of protoc-c which is incompatible with your libprotobuf-c headers. Please update your headers.
#elif 1003000 < PROTOBUF_C_MIN_COMPILER_VERSION
# error This file was generated by an older version of protoc-c which is incompatible with your libprotobuf-c headers. Please regenerate this file with a newer version of protoc-c.
#endif

#include "chk/chk.pb-c.h"

typedef struct _Srv__NotifyReadyReq Srv__NotifyReadyReq;
typedef struct _Srv__GetPoolSvcReq Srv__GetPoolSvcReq;
typedef struct _Srv__GetPoolSvcResp Srv__GetPoolSvcResp;
typedef struct _Srv__PoolFindByLabelReq Srv__PoolFindByLabelReq;
typedef struct _Srv__PoolFindByLabelResp Srv__PoolFindByLabelResp;
typedef struct _Srv__CheckListPoolReq Srv__CheckListPoolReq;
typedef struct _Srv__CheckListPoolResp Srv__CheckListPoolResp;
typedef struct _Srv__CheckListPoolResp__OnePool Srv__CheckListPoolResp__OnePool;
typedef struct _Srv__CheckRegPoolReq Srv__CheckRegPoolReq;
typedef struct _Srv__CheckRegPoolResp Srv__CheckRegPoolResp;
typedef struct _Srv__CheckDeregPoolReq Srv__CheckDeregPoolReq;
typedef struct _Srv__CheckDeregPoolResp Srv__CheckDeregPoolResp;
typedef struct _Srv__CheckReportReq Srv__CheckReportReq;
typedef struct _Srv__CheckReportResp Srv__CheckReportResp;
typedef struct _Srv__ListPoolsReq Srv__ListPoolsReq;
typedef struct _Srv__ListPoolsResp Srv__ListPoolsResp;
typedef struct _Srv__ListPoolsResp__Pool Srv__ListPoolsResp__Pool;


/* --- enums --- */


/* --- messages --- */

struct  _Srv__NotifyReadyReq
{
  ProtobufCMessage base;
  /*
   * Primary CaRT URI
   */
  char *uri;
  /*
   * Number of primary CaRT contexts
   */
  uint32_t nctxs;
  /*
   * Path to I/O Engine's dRPC listener socket
   */
  char *drpclistenersock;
  /*
   * I/O Engine instance index
   */
  uint32_t instanceidx;
  /*
   * number of VOS targets allocated in I/O Engine
   */
  uint32_t ntgts;
  /*
   * HLC incarnation number
   */
  uint64_t incarnation;
  /*
   * secondary CaRT URIs
   */
  size_t n_secondaryuris;
  char **secondaryuris;
  /*
   * number of CaRT contexts for each secondary provider
   */
  size_t n_secondarynctxs;
  uint32_t *secondarynctxs;
  /*
   * True if engine started in checker mode
   */
  protobuf_c_boolean check_mode;
};
#define SRV__NOTIFY_READY_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&srv__notify_ready_req__descriptor) \
    , (char *)protobuf_c_empty_string, 0, (char *)protobuf_c_empty_string, 0, 0, 0, 0,NULL, 0,NULL, 0 }


struct  _Srv__GetPoolSvcReq
{
  ProtobufCMessage base;
  /*
   * Pool UUID
   */
  char *uuid;
};
#define SRV__GET_POOL_SVC_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&srv__get_pool_svc_req__descriptor) \
    , (char *)protobuf_c_empty_string }


struct  _Srv__GetPoolSvcResp
{
  ProtobufCMessage base;
  /*
   * DAOS error code
   */
  int32_t status;
  /*
   * Pool service replica ranks
   */
  size_t n_svcreps;
  uint32_t *svcreps;
};
#define SRV__GET_POOL_SVC_RESP__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&srv__get_pool_svc_resp__descriptor) \
    , 0, 0,NULL }


struct  _Srv__PoolFindByLabelReq
{
  ProtobufCMessage base;
  /*
   * Pool label
   */
  char *label;
};
#define SRV__POOL_FIND_BY_LABEL_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&srv__pool_find_by_label_req__descriptor) \
    , (char *)protobuf_c_empty_string }


struct  _Srv__PoolFindByLabelResp
{
  ProtobufCMessage base;
  /*
   * DAOS error code
   */
  int32_t status;
  /*
   * Pool UUID
   */
  char *uuid;
  /*
   * Pool service replica ranks
   */
  size_t n_svcreps;
  uint32_t *svcreps;
};
#define SRV__POOL_FIND_BY_LABEL_RESP__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&srv__pool_find_by_label_resp__descriptor) \
    , 0, (char *)protobuf_c_empty_string, 0,NULL }


/*
 * List all the known pools from MS.
 */
struct  _Srv__CheckListPoolReq
{
  ProtobufCMessage base;
};
#define SRV__CHECK_LIST_POOL_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&srv__check_list_pool_req__descriptor) \
     }


struct  _Srv__CheckListPoolResp__OnePool
{
  ProtobufCMessage base;
  /*
   * Pool UUID.
   */
  char *uuid;
  /*
   * Pool label.
   */
  char *label;
  /*
   * Pool service replica ranks.
   */
  size_t n_svcreps;
  uint32_t *svcreps;
};
#define SRV__CHECK_LIST_POOL_RESP__ONE_POOL__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&srv__check_list_pool_resp__one_pool__descriptor) \
    , (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, 0,NULL }


struct  _Srv__CheckListPoolResp
{
  ProtobufCMessage base;
  /*
   * DAOS error code.
   */
  int32_t status;
  /*
   * The list of pools.
   */
  size_t n_pools;
  Srv__CheckListPoolResp__OnePool **pools;
};
#define SRV__CHECK_LIST_POOL_RESP__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&srv__check_list_pool_resp__descriptor) \
    , 0, 0,NULL }


/*
 * Register pool to MS.
 */
struct  _Srv__CheckRegPoolReq
{
  ProtobufCMessage base;
  /*
   * DAOS Check event sequence, unique for the instance.
   */
  uint64_t seq;
  /*
   * Pool UUID.
   */
  char *uuid;
  /*
   * Pool label.
   */
  char *label;
  /*
   * Pool service replica ranks.
   */
  size_t n_svcreps;
  uint32_t *svcreps;
};
#define SRV__CHECK_REG_POOL_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&srv__check_reg_pool_req__descriptor) \
    , 0, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, 0,NULL }


struct  _Srv__CheckRegPoolResp
{
  ProtobufCMessage base;
  /*
   * DAOS error code.
   */
  int32_t status;
};
#define SRV__CHECK_REG_POOL_RESP__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&srv__check_reg_pool_resp__descriptor) \
    , 0 }


/*
 * Deregister pool from MS.
 */
struct  _Srv__CheckDeregPoolReq
{
  ProtobufCMessage base;
  /*
   * DAOS Check event sequence, unique for the instance.
   */
  uint64_t seq;
  /*
   * The pool to be deregistered.
   */
  char *uuid;
};
#define SRV__CHECK_DEREG_POOL_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&srv__check_dereg_pool_req__descriptor) \
    , 0, (char *)protobuf_c_empty_string }


struct  _Srv__CheckDeregPoolResp
{
  ProtobufCMessage base;
  /*
   * DAOS error code.
   */
  int32_t status;
};
#define SRV__CHECK_DEREG_POOL_RESP__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&srv__check_dereg_pool_resp__descriptor) \
    , 0 }


struct  _Srv__CheckReportReq
{
  ProtobufCMessage base;
  /*
   * Report payload
   */
  Chk__CheckReport *report;
};
#define SRV__CHECK_REPORT_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&srv__check_report_req__descriptor) \
    , NULL }


struct  _Srv__CheckReportResp
{
  ProtobufCMessage base;
  /*
   * DAOS error code.
   */
  int32_t status;
};
#define SRV__CHECK_REPORT_RESP__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&srv__check_report_resp__descriptor) \
    , 0 }


struct  _Srv__ListPoolsReq
{
  ProtobufCMessage base;
  /*
   * Include all pools in response, regardless of state
   */
  protobuf_c_boolean include_all;
};
#define SRV__LIST_POOLS_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&srv__list_pools_req__descriptor) \
    , 0 }


struct  _Srv__ListPoolsResp__Pool
{
  ProtobufCMessage base;
  /*
   * Pool UUID
   */
  char *uuid;
  /*
   * Pool label
   */
  char *label;
  /*
   * Pool service ranks
   */
  size_t n_svcreps;
  uint32_t *svcreps;
};
#define SRV__LIST_POOLS_RESP__POOL__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&srv__list_pools_resp__pool__descriptor) \
    , (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, 0,NULL }


struct  _Srv__ListPoolsResp
{
  ProtobufCMessage base;
  /*
   * List of pools
   */
  size_t n_pools;
  Srv__ListPoolsResp__Pool **pools;
};
#define SRV__LIST_POOLS_RESP__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&srv__list_pools_resp__descriptor) \
    , 0,NULL }


/* Srv__NotifyReadyReq methods */
void   srv__notify_ready_req__init
                     (Srv__NotifyReadyReq         *message);
size_t srv__notify_ready_req__get_packed_size
                     (const Srv__NotifyReadyReq   *message);
size_t srv__notify_ready_req__pack
                     (const Srv__NotifyReadyReq   *message,
                      uint8_t             *out);
size_t srv__notify_ready_req__pack_to_buffer
                     (const Srv__NotifyReadyReq   *message,
                      ProtobufCBuffer     *buffer);
Srv__NotifyReadyReq *
       srv__notify_ready_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   srv__notify_ready_req__free_unpacked
                     (Srv__NotifyReadyReq *message,
                      ProtobufCAllocator *allocator);
/* Srv__GetPoolSvcReq methods */
void   srv__get_pool_svc_req__init
                     (Srv__GetPoolSvcReq         *message);
size_t srv__get_pool_svc_req__get_packed_size
                     (const Srv__GetPoolSvcReq   *message);
size_t srv__get_pool_svc_req__pack
                     (const Srv__GetPoolSvcReq   *message,
                      uint8_t             *out);
size_t srv__get_pool_svc_req__pack_to_buffer
                     (const Srv__GetPoolSvcReq   *message,
                      ProtobufCBuffer     *buffer);
Srv__GetPoolSvcReq *
       srv__get_pool_svc_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   srv__get_pool_svc_req__free_unpacked
                     (Srv__GetPoolSvcReq *message,
                      ProtobufCAllocator *allocator);
/* Srv__GetPoolSvcResp methods */
void   srv__get_pool_svc_resp__init
                     (Srv__GetPoolSvcResp         *message);
size_t srv__get_pool_svc_resp__get_packed_size
                     (const Srv__GetPoolSvcResp   *message);
size_t srv__get_pool_svc_resp__pack
                     (const Srv__GetPoolSvcResp   *message,
                      uint8_t             *out);
size_t srv__get_pool_svc_resp__pack_to_buffer
                     (const Srv__GetPoolSvcResp   *message,
                      ProtobufCBuffer     *buffer);
Srv__GetPoolSvcResp *
       srv__get_pool_svc_resp__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   srv__get_pool_svc_resp__free_unpacked
                     (Srv__GetPoolSvcResp *message,
                      ProtobufCAllocator *allocator);
/* Srv__PoolFindByLabelReq methods */
void   srv__pool_find_by_label_req__init
                     (Srv__PoolFindByLabelReq         *message);
size_t srv__pool_find_by_label_req__get_packed_size
                     (const Srv__PoolFindByLabelReq   *message);
size_t srv__pool_find_by_label_req__pack
                     (const Srv__PoolFindByLabelReq   *message,
                      uint8_t             *out);
size_t srv__pool_find_by_label_req__pack_to_buffer
                     (const Srv__PoolFindByLabelReq   *message,
                      ProtobufCBuffer     *buffer);
Srv__PoolFindByLabelReq *
       srv__pool_find_by_label_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   srv__pool_find_by_label_req__free_unpacked
                     (Srv__PoolFindByLabelReq *message,
                      ProtobufCAllocator *allocator);
/* Srv__PoolFindByLabelResp methods */
void   srv__pool_find_by_label_resp__init
                     (Srv__PoolFindByLabelResp         *message);
size_t srv__pool_find_by_label_resp__get_packed_size
                     (const Srv__PoolFindByLabelResp   *message);
size_t srv__pool_find_by_label_resp__pack
                     (const Srv__PoolFindByLabelResp   *message,
                      uint8_t             *out);
size_t srv__pool_find_by_label_resp__pack_to_buffer
                     (const Srv__PoolFindByLabelResp   *message,
                      ProtobufCBuffer     *buffer);
Srv__PoolFindByLabelResp *
       srv__pool_find_by_label_resp__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   srv__pool_find_by_label_resp__free_unpacked
                     (Srv__PoolFindByLabelResp *message,
                      ProtobufCAllocator *allocator);
/* Srv__CheckListPoolReq methods */
void   srv__check_list_pool_req__init
                     (Srv__CheckListPoolReq         *message);
size_t srv__check_list_pool_req__get_packed_size
                     (const Srv__CheckListPoolReq   *message);
size_t srv__check_list_pool_req__pack
                     (const Srv__CheckListPoolReq   *message,
                      uint8_t             *out);
size_t srv__check_list_pool_req__pack_to_buffer
                     (const Srv__CheckListPoolReq   *message,
                      ProtobufCBuffer     *buffer);
Srv__CheckListPoolReq *
       srv__check_list_pool_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   srv__check_list_pool_req__free_unpacked
                     (Srv__CheckListPoolReq *message,
                      ProtobufCAllocator *allocator);
/* Srv__CheckListPoolResp__OnePool methods */
void   srv__check_list_pool_resp__one_pool__init
                     (Srv__CheckListPoolResp__OnePool         *message);
/* Srv__CheckListPoolResp methods */
void   srv__check_list_pool_resp__init
                     (Srv__CheckListPoolResp         *message);
size_t srv__check_list_pool_resp__get_packed_size
                     (const Srv__CheckListPoolResp   *message);
size_t srv__check_list_pool_resp__pack
                     (const Srv__CheckListPoolResp   *message,
                      uint8_t             *out);
size_t srv__check_list_pool_resp__pack_to_buffer
                     (const Srv__CheckListPoolResp   *message,
                      ProtobufCBuffer     *buffer);
Srv__CheckListPoolResp *
       srv__check_list_pool_resp__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   srv__check_list_pool_resp__free_unpacked
                     (Srv__CheckListPoolResp *message,
                      ProtobufCAllocator *allocator);
/* Srv__CheckRegPoolReq methods */
void   srv__check_reg_pool_req__init
                     (Srv__CheckRegPoolReq         *message);
size_t srv__check_reg_pool_req__get_packed_size
                     (const Srv__CheckRegPoolReq   *message);
size_t srv__check_reg_pool_req__pack
                     (const Srv__CheckRegPoolReq   *message,
                      uint8_t             *out);
size_t srv__check_reg_pool_req__pack_to_buffer
                     (const Srv__CheckRegPoolReq   *message,
                      ProtobufCBuffer     *buffer);
Srv__CheckRegPoolReq *
       srv__check_reg_pool_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   srv__check_reg_pool_req__free_unpacked
                     (Srv__CheckRegPoolReq *message,
                      ProtobufCAllocator *allocator);
/* Srv__CheckRegPoolResp methods */
void   srv__check_reg_pool_resp__init
                     (Srv__CheckRegPoolResp         *message);
size_t srv__check_reg_pool_resp__get_packed_size
                     (const Srv__CheckRegPoolResp   *message);
size_t srv__check_reg_pool_resp__pack
                     (const Srv__CheckRegPoolResp   *message,
                      uint8_t             *out);
size_t srv__check_reg_pool_resp__pack_to_buffer
                     (const Srv__CheckRegPoolResp   *message,
                      ProtobufCBuffer     *buffer);
Srv__CheckRegPoolResp *
       srv__check_reg_pool_resp__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   srv__check_reg_pool_resp__free_unpacked
                     (Srv__CheckRegPoolResp *message,
                      ProtobufCAllocator *allocator);
/* Srv__CheckDeregPoolReq methods */
void   srv__check_dereg_pool_req__init
                     (Srv__CheckDeregPoolReq         *message);
size_t srv__check_dereg_pool_req__get_packed_size
                     (const Srv__CheckDeregPoolReq   *message);
size_t srv__check_dereg_pool_req__pack
                     (const Srv__CheckDeregPoolReq   *message,
                      uint8_t             *out);
size_t srv__check_dereg_pool_req__pack_to_buffer
                     (const Srv__CheckDeregPoolReq   *message,
                      ProtobufCBuffer     *buffer);
Srv__CheckDeregPoolReq *
       srv__check_dereg_pool_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   srv__check_dereg_pool_req__free_unpacked
                     (Srv__CheckDeregPoolReq *message,
                      ProtobufCAllocator *allocator);
/* Srv__CheckDeregPoolResp methods */
void   srv__check_dereg_pool_resp__init
                     (Srv__CheckDeregPoolResp         *message);
size_t srv__check_dereg_pool_resp__get_packed_size
                     (const Srv__CheckDeregPoolResp   *message);
size_t srv__check_dereg_pool_resp__pack
                     (const Srv__CheckDeregPoolResp   *message,
                      uint8_t             *out);
size_t srv__check_dereg_pool_resp__pack_to_buffer
                     (const Srv__CheckDeregPoolResp   *message,
                      ProtobufCBuffer     *buffer);
Srv__CheckDeregPoolResp *
       srv__check_dereg_pool_resp__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   srv__check_dereg_pool_resp__free_unpacked
                     (Srv__CheckDeregPoolResp *message,
                      ProtobufCAllocator *allocator);
/* Srv__CheckReportReq methods */
void   srv__check_report_req__init
                     (Srv__CheckReportReq         *message);
size_t srv__check_report_req__get_packed_size
                     (const Srv__CheckReportReq   *message);
size_t srv__check_report_req__pack
                     (const Srv__CheckReportReq   *message,
                      uint8_t             *out);
size_t srv__check_report_req__pack_to_buffer
                     (const Srv__CheckReportReq   *message,
                      ProtobufCBuffer     *buffer);
Srv__CheckReportReq *
       srv__check_report_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   srv__check_report_req__free_unpacked
                     (Srv__CheckReportReq *message,
                      ProtobufCAllocator *allocator);
/* Srv__CheckReportResp methods */
void   srv__check_report_resp__init
                     (Srv__CheckReportResp         *message);
size_t srv__check_report_resp__get_packed_size
                     (const Srv__CheckReportResp   *message);
size_t srv__check_report_resp__pack
                     (const Srv__CheckReportResp   *message,
                      uint8_t             *out);
size_t srv__check_report_resp__pack_to_buffer
                     (const Srv__CheckReportResp   *message,
                      ProtobufCBuffer     *buffer);
Srv__CheckReportResp *
       srv__check_report_resp__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   srv__check_report_resp__free_unpacked
                     (Srv__CheckReportResp *message,
                      ProtobufCAllocator *allocator);
/* Srv__ListPoolsReq methods */
void   srv__list_pools_req__init
                     (Srv__ListPoolsReq         *message);
size_t srv__list_pools_req__get_packed_size
                     (const Srv__ListPoolsReq   *message);
size_t srv__list_pools_req__pack
                     (const Srv__ListPoolsReq   *message,
                      uint8_t             *out);
size_t srv__list_pools_req__pack_to_buffer
                     (const Srv__ListPoolsReq   *message,
                      ProtobufCBuffer     *buffer);
Srv__ListPoolsReq *
       srv__list_pools_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   srv__list_pools_req__free_unpacked
                     (Srv__ListPoolsReq *message,
                      ProtobufCAllocator *allocator);
/* Srv__ListPoolsResp__Pool methods */
void   srv__list_pools_resp__pool__init
                     (Srv__ListPoolsResp__Pool         *message);
/* Srv__ListPoolsResp methods */
void   srv__list_pools_resp__init
                     (Srv__ListPoolsResp         *message);
size_t srv__list_pools_resp__get_packed_size
                     (const Srv__ListPoolsResp   *message);
size_t srv__list_pools_resp__pack
                     (const Srv__ListPoolsResp   *message,
                      uint8_t             *out);
size_t srv__list_pools_resp__pack_to_buffer
                     (const Srv__ListPoolsResp   *message,
                      ProtobufCBuffer     *buffer);
Srv__ListPoolsResp *
       srv__list_pools_resp__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   srv__list_pools_resp__free_unpacked
                     (Srv__ListPoolsResp *message,
                      ProtobufCAllocator *allocator);
/* --- per-message closures --- */

typedef void (*Srv__NotifyReadyReq_Closure)
                 (const Srv__NotifyReadyReq *message,
                  void *closure_data);
typedef void (*Srv__GetPoolSvcReq_Closure)
                 (const Srv__GetPoolSvcReq *message,
                  void *closure_data);
typedef void (*Srv__GetPoolSvcResp_Closure)
                 (const Srv__GetPoolSvcResp *message,
                  void *closure_data);
typedef void (*Srv__PoolFindByLabelReq_Closure)
                 (const Srv__PoolFindByLabelReq *message,
                  void *closure_data);
typedef void (*Srv__PoolFindByLabelResp_Closure)
                 (const Srv__PoolFindByLabelResp *message,
                  void *closure_data);
typedef void (*Srv__CheckListPoolReq_Closure)
                 (const Srv__CheckListPoolReq *message,
                  void *closure_data);
typedef void (*Srv__CheckListPoolResp__OnePool_Closure)
                 (const Srv__CheckListPoolResp__OnePool *message,
                  void *closure_data);
typedef void (*Srv__CheckListPoolResp_Closure)
                 (const Srv__CheckListPoolResp *message,
                  void *closure_data);
typedef void (*Srv__CheckRegPoolReq_Closure)
                 (const Srv__CheckRegPoolReq *message,
                  void *closure_data);
typedef void (*Srv__CheckRegPoolResp_Closure)
                 (const Srv__CheckRegPoolResp *message,
                  void *closure_data);
typedef void (*Srv__CheckDeregPoolReq_Closure)
                 (const Srv__CheckDeregPoolReq *message,
                  void *closure_data);
typedef void (*Srv__CheckDeregPoolResp_Closure)
                 (const Srv__CheckDeregPoolResp *message,
                  void *closure_data);
typedef void (*Srv__CheckReportReq_Closure)
                 (const Srv__CheckReportReq *message,
                  void *closure_data);
typedef void (*Srv__CheckReportResp_Closure)
                 (const Srv__CheckReportResp *message,
                  void *closure_data);
typedef void (*Srv__ListPoolsReq_Closure)
                 (const Srv__ListPoolsReq *message,
                  void *closure_data);
typedef void (*Srv__ListPoolsResp__Pool_Closure)
                 (const Srv__ListPoolsResp__Pool *message,
                  void *closure_data);
typedef void (*Srv__ListPoolsResp_Closure)
                 (const Srv__ListPoolsResp *message,
                  void *closure_data);

/* --- services --- */


/* --- descriptors --- */

extern const ProtobufCMessageDescriptor srv__notify_ready_req__descriptor;
extern const ProtobufCMessageDescriptor srv__get_pool_svc_req__descriptor;
extern const ProtobufCMessageDescriptor srv__get_pool_svc_resp__descriptor;
extern const ProtobufCMessageDescriptor srv__pool_find_by_label_req__descriptor;
extern const ProtobufCMessageDescriptor srv__pool_find_by_label_resp__descriptor;
extern const ProtobufCMessageDescriptor srv__check_list_pool_req__descriptor;
extern const ProtobufCMessageDescriptor srv__check_list_pool_resp__descriptor;
extern const ProtobufCMessageDescriptor srv__check_list_pool_resp__one_pool__descriptor;
extern const ProtobufCMessageDescriptor srv__check_reg_pool_req__descriptor;
extern const ProtobufCMessageDescriptor srv__check_reg_pool_resp__descriptor;
extern const ProtobufCMessageDescriptor srv__check_dereg_pool_req__descriptor;
extern const ProtobufCMessageDescriptor srv__check_dereg_pool_resp__descriptor;
extern const ProtobufCMessageDescriptor srv__check_report_req__descriptor;
extern const ProtobufCMessageDescriptor srv__check_report_resp__descriptor;
extern const ProtobufCMessageDescriptor srv__list_pools_req__descriptor;
extern const ProtobufCMessageDescriptor srv__list_pools_resp__descriptor;
extern const ProtobufCMessageDescriptor srv__list_pools_resp__pool__descriptor;

PROTOBUF_C__END_DECLS


#endif  /* PROTOBUF_C_srv_2eproto__INCLUDED */
