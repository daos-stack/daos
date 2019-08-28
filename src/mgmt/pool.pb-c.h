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


typedef struct _Mgmt__CreatePoolReq Mgmt__CreatePoolReq;
typedef struct _Mgmt__CreatePoolResp Mgmt__CreatePoolResp;
typedef struct _Mgmt__DestroyPoolReq Mgmt__DestroyPoolReq;
typedef struct _Mgmt__DestroyPoolResp Mgmt__DestroyPoolResp;


/* --- enums --- */


/* --- messages --- */

/*
 * CreatePoolReq supplies new pool parameters.
 */
struct  _Mgmt__CreatePoolReq
{
  ProtobufCMessage base;
  uint64_t scmbytes;
  uint64_t nvmebytes;
  /*
   * comma separated integers
   */
  char *ranks;
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
   * DAOS system identifier
   */
  char *sys;
  /*
   * Access Control Entries in short string format
   */
  size_t n_acl;
  char **acl;
};
#define MGMT__CREATE_POOL_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__create_pool_req__descriptor) \
    , 0, 0, (char *)protobuf_c_empty_string, 0, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, 0,NULL }


/*
 * CreatePoolResp returns created pool uuid and ranks.
 */
struct  _Mgmt__CreatePoolResp
{
  ProtobufCMessage base;
  /*
   * DAOS error code
   */
  int32_t status;
  /*
   * new pool's uuid
   */
  char *uuid;
  /*
   * comma separated integers
   */
  char *svcreps;
};
#define MGMT__CREATE_POOL_RESP__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__create_pool_resp__descriptor) \
    , 0, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string }


/*
 * DestroyPoolReq supplies pool identifier and force flag.
 */
struct  _Mgmt__DestroyPoolReq
{
  ProtobufCMessage base;
  /*
   * uuid of pool to destroy
   */
  char *uuid;
  /*
   * DAOS system identifier
   */
  char *sys;
  /*
   * destroy regardless of active connections
   */
  protobuf_c_boolean force;
};
#define MGMT__DESTROY_POOL_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__destroy_pool_req__descriptor) \
    , (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, 0 }


/*
 * DestroyPoolResp returns resultant state of destroy operation.
 */
struct  _Mgmt__DestroyPoolResp
{
  ProtobufCMessage base;
  /*
   * DAOS error code
   */
  int32_t status;
};
#define MGMT__DESTROY_POOL_RESP__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__destroy_pool_resp__descriptor) \
    , 0 }


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
/* Mgmt__DestroyPoolReq methods */
void   mgmt__destroy_pool_req__init
                     (Mgmt__DestroyPoolReq         *message);
size_t mgmt__destroy_pool_req__get_packed_size
                     (const Mgmt__DestroyPoolReq   *message);
size_t mgmt__destroy_pool_req__pack
                     (const Mgmt__DestroyPoolReq   *message,
                      uint8_t             *out);
size_t mgmt__destroy_pool_req__pack_to_buffer
                     (const Mgmt__DestroyPoolReq   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__DestroyPoolReq *
       mgmt__destroy_pool_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__destroy_pool_req__free_unpacked
                     (Mgmt__DestroyPoolReq *message,
                      ProtobufCAllocator *allocator);
/* Mgmt__DestroyPoolResp methods */
void   mgmt__destroy_pool_resp__init
                     (Mgmt__DestroyPoolResp         *message);
size_t mgmt__destroy_pool_resp__get_packed_size
                     (const Mgmt__DestroyPoolResp   *message);
size_t mgmt__destroy_pool_resp__pack
                     (const Mgmt__DestroyPoolResp   *message,
                      uint8_t             *out);
size_t mgmt__destroy_pool_resp__pack_to_buffer
                     (const Mgmt__DestroyPoolResp   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__DestroyPoolResp *
       mgmt__destroy_pool_resp__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__destroy_pool_resp__free_unpacked
                     (Mgmt__DestroyPoolResp *message,
                      ProtobufCAllocator *allocator);
/* --- per-message closures --- */

typedef void (*Mgmt__CreatePoolReq_Closure)
                 (const Mgmt__CreatePoolReq *message,
                  void *closure_data);
typedef void (*Mgmt__CreatePoolResp_Closure)
                 (const Mgmt__CreatePoolResp *message,
                  void *closure_data);
typedef void (*Mgmt__DestroyPoolReq_Closure)
                 (const Mgmt__DestroyPoolReq *message,
                  void *closure_data);
typedef void (*Mgmt__DestroyPoolResp_Closure)
                 (const Mgmt__DestroyPoolResp *message,
                  void *closure_data);

/* --- services --- */


/* --- descriptors --- */

extern const ProtobufCMessageDescriptor mgmt__create_pool_req__descriptor;
extern const ProtobufCMessageDescriptor mgmt__create_pool_resp__descriptor;
extern const ProtobufCMessageDescriptor mgmt__destroy_pool_req__descriptor;
extern const ProtobufCMessageDescriptor mgmt__destroy_pool_resp__descriptor;

PROTOBUF_C__END_DECLS


#endif  /* PROTOBUF_C_pool_2eproto__INCLUDED */
