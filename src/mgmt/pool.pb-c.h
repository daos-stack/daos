/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: pool.proto */

#ifndef PROTOBUF_C_pool_2eproto__INCLUDED
#define PROTOBUF_C_pool_2eproto__INCLUDED

#include <protobuf-c/protobuf-c.h>

PROTOBUF_C__BEGIN_DECLS

#if PROTOBUF_C_VERSION_NUMBER < 1003000
# error This file was generated by a newer version of protoc-c which is incompatible with your libprotobuf-c headers. Please update your headers.
#elif 1003001 < PROTOBUF_C_MIN_COMPILER_VERSION
# error This file was generated by an older version of protoc-c which is incompatible with your libprotobuf-c headers. Please regenerate this file with a newer version of protoc-c.
#endif

#include "srv.pb-c.h"

typedef struct _Mgmt__CreatePoolReq Mgmt__CreatePoolReq;
typedef struct _Mgmt__CreatePoolResp Mgmt__CreatePoolResp;


/* --- enums --- */


/* --- messages --- */

struct  _Mgmt__CreatePoolReq
{
  ProtobufCMessage base;
  uint64_t scmbytes;
  uint64_t nvmebytes;
  /*
   * colon separated integers
   */
  size_t n_ranks;
  uint32_t *ranks;
  /*
   * desired number of pool service replicas
   */
  uint32_t numsvcreps;
  /*
   * formatted user e.g. "bob@"
   */
  char *user;
  /*
   * formatted group e.g. "builders@"
   */
  char *usergroup;
  /*
   * DAOS process group identifier
   */
  char *procgroup;
};
#define MGMT__CREATE_POOL_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__create_pool_req__descriptor) \
    , 0, 0, 0,NULL, 0, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string }


/*
 * CreatePoolResp returns created pool uuid and ranks.
 */
struct  _Mgmt__CreatePoolResp
{
  ProtobufCMessage base;
  Mgmt__DaosRequestStatus status;
  /*
   * new pool's uuid
   */
  char *uuid;
  /*
   * colon separated integers
   */
  char *ranklist;
};
#define MGMT__CREATE_POOL_RESP__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__create_pool_resp__descriptor) \
    , MGMT__DAOS_REQUEST_STATUS__SUCCESS, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string }


/* Mgmt__CreatePoolReq methods */
void   mgmt__create_pool_req__init
                     (Mgmt__CreatePoolReq         *message);
size_t mgmt__create_pool_req__get_packed_size
                     (const Mgmt__CreatePoolReq   *message);
size_t mgmt__create_pool_req__pack
                     (const Mgmt__CreatePoolReq   *message,
                      uint8_t             *out);
size_t mgmt__create_pool_req__pack_to_buffer
                     (const Mgmt__CreatePoolReq   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__CreatePoolReq *
       mgmt__create_pool_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__create_pool_req__free_unpacked
                     (Mgmt__CreatePoolReq *message,
                      ProtobufCAllocator *allocator);
/* Mgmt__CreatePoolResp methods */
void   mgmt__create_pool_resp__init
                     (Mgmt__CreatePoolResp         *message);
size_t mgmt__create_pool_resp__get_packed_size
                     (const Mgmt__CreatePoolResp   *message);
size_t mgmt__create_pool_resp__pack
                     (const Mgmt__CreatePoolResp   *message,
                      uint8_t             *out);
size_t mgmt__create_pool_resp__pack_to_buffer
                     (const Mgmt__CreatePoolResp   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__CreatePoolResp *
       mgmt__create_pool_resp__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__create_pool_resp__free_unpacked
                     (Mgmt__CreatePoolResp *message,
                      ProtobufCAllocator *allocator);
/* --- per-message closures --- */

typedef void (*Mgmt__CreatePoolReq_Closure)
                 (const Mgmt__CreatePoolReq *message,
                  void *closure_data);
typedef void (*Mgmt__CreatePoolResp_Closure)
                 (const Mgmt__CreatePoolResp *message,
                  void *closure_data);

/* --- services --- */


/* --- descriptors --- */

extern const ProtobufCMessageDescriptor mgmt__create_pool_req__descriptor;
extern const ProtobufCMessageDescriptor mgmt__create_pool_resp__descriptor;

PROTOBUF_C__END_DECLS


#endif  /* PROTOBUF_C_pool_2eproto__INCLUDED */
