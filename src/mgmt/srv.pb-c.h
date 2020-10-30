/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: srv.proto */

#ifndef PROTOBUF_C_srv_2eproto__INCLUDED
#define PROTOBUF_C_srv_2eproto__INCLUDED

#include <protobuf-c/protobuf-c.h>

PROTOBUF_C__BEGIN_DECLS

#if PROTOBUF_C_VERSION_NUMBER < 1003000
# error This file was generated by a newer version of protoc-c which is incompatible with your libprotobuf-c headers. Please update your headers.
#elif 1003003 < PROTOBUF_C_MIN_COMPILER_VERSION
# error This file was generated by an older version of protoc-c which is incompatible with your libprotobuf-c headers. Please regenerate this file with a newer version of protoc-c.
#endif


typedef struct _Mgmt__DaosResp Mgmt__DaosResp;
typedef struct _Mgmt__GroupUpdateReq Mgmt__GroupUpdateReq;
typedef struct _Mgmt__GroupUpdateReq__Server Mgmt__GroupUpdateReq__Server;
typedef struct _Mgmt__GroupUpdateResp Mgmt__GroupUpdateResp;
typedef struct _Mgmt__JoinReq Mgmt__JoinReq;
typedef struct _Mgmt__JoinResp Mgmt__JoinResp;
typedef struct _Mgmt__LeaderQueryReq Mgmt__LeaderQueryReq;
typedef struct _Mgmt__LeaderQueryResp Mgmt__LeaderQueryResp;
typedef struct _Mgmt__GetAttachInfoReq Mgmt__GetAttachInfoReq;
typedef struct _Mgmt__GetAttachInfoResp Mgmt__GetAttachInfoResp;
typedef struct _Mgmt__GetAttachInfoResp__Psr Mgmt__GetAttachInfoResp__Psr;
typedef struct _Mgmt__PrepShutdownReq Mgmt__PrepShutdownReq;
typedef struct _Mgmt__PingRankReq Mgmt__PingRankReq;
typedef struct _Mgmt__SetRankReq Mgmt__SetRankReq;
typedef struct _Mgmt__CreateMsReq Mgmt__CreateMsReq;


/* --- enums --- */

typedef enum _Mgmt__JoinResp__State {
  /*
   * Server in the system.
   */
  MGMT__JOIN_RESP__STATE__IN = 0,
  /*
   * Server excluded from the system.
   */
  MGMT__JOIN_RESP__STATE__OUT = 1
    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(MGMT__JOIN_RESP__STATE)
} Mgmt__JoinResp__State;

/* --- messages --- */

/*
 * Generic response just containing DER from IO server.
 */
struct  _Mgmt__DaosResp
{
  ProtobufCMessage base;
  /*
   * DAOS error code.
   */
  int32_t status;
};
#define MGMT__DAOS_RESP__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__daos_resp__descriptor) \
    , 0 }


struct  _Mgmt__GroupUpdateReq__Server
{
  ProtobufCMessage base;
  uint32_t rank;
  char *uri;
};
#define MGMT__GROUP_UPDATE_REQ__SERVER__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__group_update_req__server__descriptor) \
    , 0, (char *)protobuf_c_empty_string }


struct  _Mgmt__GroupUpdateReq
{
  ProtobufCMessage base;
  uint32_t map_version;
  size_t n_servers;
  Mgmt__GroupUpdateReq__Server **servers;
};
#define MGMT__GROUP_UPDATE_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__group_update_req__descriptor) \
    , 0, 0,NULL }


struct  _Mgmt__GroupUpdateResp
{
  ProtobufCMessage base;
  int32_t status;
};
#define MGMT__GROUP_UPDATE_RESP__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__group_update_resp__descriptor) \
    , 0 }


struct  _Mgmt__JoinReq
{
  ProtobufCMessage base;
  /*
   * Servee UUID.
   */
  char *uuid;
  /*
   * Server rank desired, if not -1.
   */
  uint32_t rank;
  /*
   * Server CaRT base URI (i.e., for context 0).
   */
  char *uri;
  /*
   * Server CaRT context count.
   */
  uint32_t nctxs;
  /*
   * Server management address.
   */
  char *addr;
  /*
   * Fault domain for this instance's server
   */
  char *srvfaultdomain;
};
#define MGMT__JOIN_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__join_req__descriptor) \
    , (char *)protobuf_c_empty_string, 0, (char *)protobuf_c_empty_string, 0, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string }


struct  _Mgmt__JoinResp
{
  ProtobufCMessage base;
  /*
   * DAOS error code
   */
  int32_t status;
  /*
   * Server rank assigned.
   */
  uint32_t rank;
  /*
   * Server state in the system map.
   */
  Mgmt__JoinResp__State state;
  /*
   * Fault domain for the instance
   */
  char *faultdomain;
};
#define MGMT__JOIN_RESP__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__join_resp__descriptor) \
    , 0, 0, MGMT__JOIN_RESP__STATE__IN, (char *)protobuf_c_empty_string }


struct  _Mgmt__LeaderQueryReq
{
  ProtobufCMessage base;
  /*
   * System name.
   */
  char *system;
};
#define MGMT__LEADER_QUERY_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__leader_query_req__descriptor) \
    , (char *)protobuf_c_empty_string }


struct  _Mgmt__LeaderQueryResp
{
  ProtobufCMessage base;
  char *currentleader;
  size_t n_replicas;
  char **replicas;
};
#define MGMT__LEADER_QUERY_RESP__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__leader_query_resp__descriptor) \
    , (char *)protobuf_c_empty_string, 0,NULL }


struct  _Mgmt__GetAttachInfoReq
{
  ProtobufCMessage base;
  /*
   * System name. For daos_agent only.
   */
  char *sys;
  /*
   * Return PSRs for all ranks, not just the MS replicas.
   */
  protobuf_c_boolean allranks;
  /*
   * Job ID to associate instance with.
   */
  char *jobid;
};
#define MGMT__GET_ATTACH_INFO_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__get_attach_info_req__descriptor) \
    , (char *)protobuf_c_empty_string, 0, (char *)protobuf_c_empty_string }


struct  _Mgmt__GetAttachInfoResp__Psr
{
  ProtobufCMessage base;
  uint32_t rank;
  char *uri;
};
#define MGMT__GET_ATTACH_INFO_RESP__PSR__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__get_attach_info_resp__psr__descriptor) \
    , 0, (char *)protobuf_c_empty_string }


struct  _Mgmt__GetAttachInfoResp
{
  ProtobufCMessage base;
  /*
   * DAOS error code
   */
  int32_t status;
  /*
   * CaRT PSRs of the system group.
   */
  size_t n_psrs;
  Mgmt__GetAttachInfoResp__Psr **psrs;
  /*
   * These CaRT settings are shared with the
   * libdaos client to aid in CaRT initialization.
   */
  /*
   * CaRT OFI provider
   */
  char *provider;
  /*
   * CaRT OFI_INTERFACE
   */
  char *interface;
  /*
   * CaRT OFI_DOMAIN for given OFI_INTERFACE
   */
  char *domain;
  /*
   * CaRT CRT_CTX_SHARE_ADDR
   */
  uint32_t crtctxshareaddr;
  /*
   * CaRT CRT_TIMEOUT
   */
  uint32_t crttimeout;
  /*
   * ARP protocol hardware identifier of the
   */
  uint32_t netdevclass;
};
#define MGMT__GET_ATTACH_INFO_RESP__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__get_attach_info_resp__descriptor) \
    , 0, 0,NULL, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, 0, 0, 0 }


struct  _Mgmt__PrepShutdownReq
{
  ProtobufCMessage base;
  /*
   * DAOS IO server unique identifier.
   */
  uint32_t rank;
};
#define MGMT__PREP_SHUTDOWN_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__prep_shutdown_req__descriptor) \
    , 0 }


struct  _Mgmt__PingRankReq
{
  ProtobufCMessage base;
  /*
   * DAOS IO server unique identifier.
   */
  uint32_t rank;
};
#define MGMT__PING_RANK_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__ping_rank_req__descriptor) \
    , 0 }


struct  _Mgmt__SetRankReq
{
  ProtobufCMessage base;
  /*
   * DAOS IO server unique identifier.
   */
  uint32_t rank;
};
#define MGMT__SET_RANK_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__set_rank_req__descriptor) \
    , 0 }


struct  _Mgmt__CreateMsReq
{
  ProtobufCMessage base;
  /*
   * Bootstrap the DAOS management service (MS).
   */
  protobuf_c_boolean bootstrap;
  /*
   * DAOS IO server UUID of this MS replica.
   */
  char *uuid;
  /*
   * Control server management address of this MS replica.
   */
  char *addr;
};
#define MGMT__CREATE_MS_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__create_ms_req__descriptor) \
    , 0, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string }


/* Mgmt__DaosResp methods */
void   mgmt__daos_resp__init
                     (Mgmt__DaosResp         *message);
size_t mgmt__daos_resp__get_packed_size
                     (const Mgmt__DaosResp   *message);
size_t mgmt__daos_resp__pack
                     (const Mgmt__DaosResp   *message,
                      uint8_t             *out);
size_t mgmt__daos_resp__pack_to_buffer
                     (const Mgmt__DaosResp   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__DaosResp *
       mgmt__daos_resp__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__daos_resp__free_unpacked
                     (Mgmt__DaosResp *message,
                      ProtobufCAllocator *allocator);
/* Mgmt__GroupUpdateReq__Server methods */
void   mgmt__group_update_req__server__init
                     (Mgmt__GroupUpdateReq__Server         *message);
/* Mgmt__GroupUpdateReq methods */
void   mgmt__group_update_req__init
                     (Mgmt__GroupUpdateReq         *message);
size_t mgmt__group_update_req__get_packed_size
                     (const Mgmt__GroupUpdateReq   *message);
size_t mgmt__group_update_req__pack
                     (const Mgmt__GroupUpdateReq   *message,
                      uint8_t             *out);
size_t mgmt__group_update_req__pack_to_buffer
                     (const Mgmt__GroupUpdateReq   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__GroupUpdateReq *
       mgmt__group_update_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__group_update_req__free_unpacked
                     (Mgmt__GroupUpdateReq *message,
                      ProtobufCAllocator *allocator);
/* Mgmt__GroupUpdateResp methods */
void   mgmt__group_update_resp__init
                     (Mgmt__GroupUpdateResp         *message);
size_t mgmt__group_update_resp__get_packed_size
                     (const Mgmt__GroupUpdateResp   *message);
size_t mgmt__group_update_resp__pack
                     (const Mgmt__GroupUpdateResp   *message,
                      uint8_t             *out);
size_t mgmt__group_update_resp__pack_to_buffer
                     (const Mgmt__GroupUpdateResp   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__GroupUpdateResp *
       mgmt__group_update_resp__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__group_update_resp__free_unpacked
                     (Mgmt__GroupUpdateResp *message,
                      ProtobufCAllocator *allocator);
/* Mgmt__JoinReq methods */
void   mgmt__join_req__init
                     (Mgmt__JoinReq         *message);
size_t mgmt__join_req__get_packed_size
                     (const Mgmt__JoinReq   *message);
size_t mgmt__join_req__pack
                     (const Mgmt__JoinReq   *message,
                      uint8_t             *out);
size_t mgmt__join_req__pack_to_buffer
                     (const Mgmt__JoinReq   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__JoinReq *
       mgmt__join_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__join_req__free_unpacked
                     (Mgmt__JoinReq *message,
                      ProtobufCAllocator *allocator);
/* Mgmt__JoinResp methods */
void   mgmt__join_resp__init
                     (Mgmt__JoinResp         *message);
size_t mgmt__join_resp__get_packed_size
                     (const Mgmt__JoinResp   *message);
size_t mgmt__join_resp__pack
                     (const Mgmt__JoinResp   *message,
                      uint8_t             *out);
size_t mgmt__join_resp__pack_to_buffer
                     (const Mgmt__JoinResp   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__JoinResp *
       mgmt__join_resp__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__join_resp__free_unpacked
                     (Mgmt__JoinResp *message,
                      ProtobufCAllocator *allocator);
/* Mgmt__LeaderQueryReq methods */
void   mgmt__leader_query_req__init
                     (Mgmt__LeaderQueryReq         *message);
size_t mgmt__leader_query_req__get_packed_size
                     (const Mgmt__LeaderQueryReq   *message);
size_t mgmt__leader_query_req__pack
                     (const Mgmt__LeaderQueryReq   *message,
                      uint8_t             *out);
size_t mgmt__leader_query_req__pack_to_buffer
                     (const Mgmt__LeaderQueryReq   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__LeaderQueryReq *
       mgmt__leader_query_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__leader_query_req__free_unpacked
                     (Mgmt__LeaderQueryReq *message,
                      ProtobufCAllocator *allocator);
/* Mgmt__LeaderQueryResp methods */
void   mgmt__leader_query_resp__init
                     (Mgmt__LeaderQueryResp         *message);
size_t mgmt__leader_query_resp__get_packed_size
                     (const Mgmt__LeaderQueryResp   *message);
size_t mgmt__leader_query_resp__pack
                     (const Mgmt__LeaderQueryResp   *message,
                      uint8_t             *out);
size_t mgmt__leader_query_resp__pack_to_buffer
                     (const Mgmt__LeaderQueryResp   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__LeaderQueryResp *
       mgmt__leader_query_resp__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__leader_query_resp__free_unpacked
                     (Mgmt__LeaderQueryResp *message,
                      ProtobufCAllocator *allocator);
/* Mgmt__GetAttachInfoReq methods */
void   mgmt__get_attach_info_req__init
                     (Mgmt__GetAttachInfoReq         *message);
size_t mgmt__get_attach_info_req__get_packed_size
                     (const Mgmt__GetAttachInfoReq   *message);
size_t mgmt__get_attach_info_req__pack
                     (const Mgmt__GetAttachInfoReq   *message,
                      uint8_t             *out);
size_t mgmt__get_attach_info_req__pack_to_buffer
                     (const Mgmt__GetAttachInfoReq   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__GetAttachInfoReq *
       mgmt__get_attach_info_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__get_attach_info_req__free_unpacked
                     (Mgmt__GetAttachInfoReq *message,
                      ProtobufCAllocator *allocator);
/* Mgmt__GetAttachInfoResp__Psr methods */
void   mgmt__get_attach_info_resp__psr__init
                     (Mgmt__GetAttachInfoResp__Psr         *message);
/* Mgmt__GetAttachInfoResp methods */
void   mgmt__get_attach_info_resp__init
                     (Mgmt__GetAttachInfoResp         *message);
size_t mgmt__get_attach_info_resp__get_packed_size
                     (const Mgmt__GetAttachInfoResp   *message);
size_t mgmt__get_attach_info_resp__pack
                     (const Mgmt__GetAttachInfoResp   *message,
                      uint8_t             *out);
size_t mgmt__get_attach_info_resp__pack_to_buffer
                     (const Mgmt__GetAttachInfoResp   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__GetAttachInfoResp *
       mgmt__get_attach_info_resp__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__get_attach_info_resp__free_unpacked
                     (Mgmt__GetAttachInfoResp *message,
                      ProtobufCAllocator *allocator);
/* Mgmt__PrepShutdownReq methods */
void   mgmt__prep_shutdown_req__init
                     (Mgmt__PrepShutdownReq         *message);
size_t mgmt__prep_shutdown_req__get_packed_size
                     (const Mgmt__PrepShutdownReq   *message);
size_t mgmt__prep_shutdown_req__pack
                     (const Mgmt__PrepShutdownReq   *message,
                      uint8_t             *out);
size_t mgmt__prep_shutdown_req__pack_to_buffer
                     (const Mgmt__PrepShutdownReq   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__PrepShutdownReq *
       mgmt__prep_shutdown_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__prep_shutdown_req__free_unpacked
                     (Mgmt__PrepShutdownReq *message,
                      ProtobufCAllocator *allocator);
/* Mgmt__PingRankReq methods */
void   mgmt__ping_rank_req__init
                     (Mgmt__PingRankReq         *message);
size_t mgmt__ping_rank_req__get_packed_size
                     (const Mgmt__PingRankReq   *message);
size_t mgmt__ping_rank_req__pack
                     (const Mgmt__PingRankReq   *message,
                      uint8_t             *out);
size_t mgmt__ping_rank_req__pack_to_buffer
                     (const Mgmt__PingRankReq   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__PingRankReq *
       mgmt__ping_rank_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__ping_rank_req__free_unpacked
                     (Mgmt__PingRankReq *message,
                      ProtobufCAllocator *allocator);
/* Mgmt__SetRankReq methods */
void   mgmt__set_rank_req__init
                     (Mgmt__SetRankReq         *message);
size_t mgmt__set_rank_req__get_packed_size
                     (const Mgmt__SetRankReq   *message);
size_t mgmt__set_rank_req__pack
                     (const Mgmt__SetRankReq   *message,
                      uint8_t             *out);
size_t mgmt__set_rank_req__pack_to_buffer
                     (const Mgmt__SetRankReq   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__SetRankReq *
       mgmt__set_rank_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__set_rank_req__free_unpacked
                     (Mgmt__SetRankReq *message,
                      ProtobufCAllocator *allocator);
/* Mgmt__CreateMsReq methods */
void   mgmt__create_ms_req__init
                     (Mgmt__CreateMsReq         *message);
size_t mgmt__create_ms_req__get_packed_size
                     (const Mgmt__CreateMsReq   *message);
size_t mgmt__create_ms_req__pack
                     (const Mgmt__CreateMsReq   *message,
                      uint8_t             *out);
size_t mgmt__create_ms_req__pack_to_buffer
                     (const Mgmt__CreateMsReq   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__CreateMsReq *
       mgmt__create_ms_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__create_ms_req__free_unpacked
                     (Mgmt__CreateMsReq *message,
                      ProtobufCAllocator *allocator);
/* --- per-message closures --- */

typedef void (*Mgmt__DaosResp_Closure)
                 (const Mgmt__DaosResp *message,
                  void *closure_data);
typedef void (*Mgmt__GroupUpdateReq__Server_Closure)
                 (const Mgmt__GroupUpdateReq__Server *message,
                  void *closure_data);
typedef void (*Mgmt__GroupUpdateReq_Closure)
                 (const Mgmt__GroupUpdateReq *message,
                  void *closure_data);
typedef void (*Mgmt__GroupUpdateResp_Closure)
                 (const Mgmt__GroupUpdateResp *message,
                  void *closure_data);
typedef void (*Mgmt__JoinReq_Closure)
                 (const Mgmt__JoinReq *message,
                  void *closure_data);
typedef void (*Mgmt__JoinResp_Closure)
                 (const Mgmt__JoinResp *message,
                  void *closure_data);
typedef void (*Mgmt__LeaderQueryReq_Closure)
                 (const Mgmt__LeaderQueryReq *message,
                  void *closure_data);
typedef void (*Mgmt__LeaderQueryResp_Closure)
                 (const Mgmt__LeaderQueryResp *message,
                  void *closure_data);
typedef void (*Mgmt__GetAttachInfoReq_Closure)
                 (const Mgmt__GetAttachInfoReq *message,
                  void *closure_data);
typedef void (*Mgmt__GetAttachInfoResp__Psr_Closure)
                 (const Mgmt__GetAttachInfoResp__Psr *message,
                  void *closure_data);
typedef void (*Mgmt__GetAttachInfoResp_Closure)
                 (const Mgmt__GetAttachInfoResp *message,
                  void *closure_data);
typedef void (*Mgmt__PrepShutdownReq_Closure)
                 (const Mgmt__PrepShutdownReq *message,
                  void *closure_data);
typedef void (*Mgmt__PingRankReq_Closure)
                 (const Mgmt__PingRankReq *message,
                  void *closure_data);
typedef void (*Mgmt__SetRankReq_Closure)
                 (const Mgmt__SetRankReq *message,
                  void *closure_data);
typedef void (*Mgmt__CreateMsReq_Closure)
                 (const Mgmt__CreateMsReq *message,
                  void *closure_data);

/* --- services --- */


/* --- descriptors --- */

extern const ProtobufCMessageDescriptor mgmt__daos_resp__descriptor;
extern const ProtobufCMessageDescriptor mgmt__group_update_req__descriptor;
extern const ProtobufCMessageDescriptor mgmt__group_update_req__server__descriptor;
extern const ProtobufCMessageDescriptor mgmt__group_update_resp__descriptor;
extern const ProtobufCMessageDescriptor mgmt__join_req__descriptor;
extern const ProtobufCMessageDescriptor mgmt__join_resp__descriptor;
extern const ProtobufCEnumDescriptor    mgmt__join_resp__state__descriptor;
extern const ProtobufCMessageDescriptor mgmt__leader_query_req__descriptor;
extern const ProtobufCMessageDescriptor mgmt__leader_query_resp__descriptor;
extern const ProtobufCMessageDescriptor mgmt__get_attach_info_req__descriptor;
extern const ProtobufCMessageDescriptor mgmt__get_attach_info_resp__descriptor;
extern const ProtobufCMessageDescriptor mgmt__get_attach_info_resp__psr__descriptor;
extern const ProtobufCMessageDescriptor mgmt__prep_shutdown_req__descriptor;
extern const ProtobufCMessageDescriptor mgmt__ping_rank_req__descriptor;
extern const ProtobufCMessageDescriptor mgmt__set_rank_req__descriptor;
extern const ProtobufCMessageDescriptor mgmt__create_ms_req__descriptor;

PROTOBUF_C__END_DECLS


#endif  /* PROTOBUF_C_srv_2eproto__INCLUDED */
