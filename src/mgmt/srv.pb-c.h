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


typedef struct _Proto__DaosRank Proto__DaosRank;
typedef struct _Proto__DaosResponse Proto__DaosResponse;


/* --- enums --- */

typedef enum _Proto__DaosRequestStatus {
  PROTO__DAOS_REQUEST_STATUS__SUCCESS = 0,
  /*
   * Unknown error
   */
  PROTO__DAOS_REQUEST_STATUS__ERR_UNKNOWN = -1,
  /*
   * Rank requested is invalid
   */
  PROTO__DAOS_REQUEST_STATUS__ERR_INVALID_RANK = -2,
  /*
   * Pool UUID requested is invalid
   */
  PROTO__DAOS_REQUEST_STATUS__ERR_INVALID_UUID = -3
    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(PROTO__DAOS_REQUEST_STATUS)
} Proto__DaosRequestStatus;

/* --- messages --- */

/*
 * Identifier for server rank within DAOS pool
 */
struct  _Proto__DaosRank
{
  ProtobufCMessage base;
  /*
   * UUID of the pool
   */
  char *pool_uuid;
  /*
   * Server rank
   */
  uint32_t rank;
};
#define PROTO__DAOS_RANK__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&proto__daos_rank__descriptor) \
    , (char *)protobuf_c_empty_string, 0 }


struct  _Proto__DaosResponse
{
  ProtobufCMessage base;
  Proto__DaosRequestStatus status;
};
#define PROTO__DAOS_RESPONSE__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&proto__daos_response__descriptor) \
    , PROTO__DAOS_REQUEST_STATUS__SUCCESS }


/* Proto__DaosRank methods */
void   proto__daos_rank__init
                     (Proto__DaosRank         *message);
size_t proto__daos_rank__get_packed_size
                     (const Proto__DaosRank   *message);
size_t proto__daos_rank__pack
                     (const Proto__DaosRank   *message,
                      uint8_t             *out);
size_t proto__daos_rank__pack_to_buffer
                     (const Proto__DaosRank   *message,
                      ProtobufCBuffer     *buffer);
Proto__DaosRank *
       proto__daos_rank__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   proto__daos_rank__free_unpacked
                     (Proto__DaosRank *message,
                      ProtobufCAllocator *allocator);
/* Proto__DaosResponse methods */
void   proto__daos_response__init
                     (Proto__DaosResponse         *message);
size_t proto__daos_response__get_packed_size
                     (const Proto__DaosResponse   *message);
size_t proto__daos_response__pack
                     (const Proto__DaosResponse   *message,
                      uint8_t             *out);
size_t proto__daos_response__pack_to_buffer
                     (const Proto__DaosResponse   *message,
                      ProtobufCBuffer     *buffer);
Proto__DaosResponse *
       proto__daos_response__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   proto__daos_response__free_unpacked
                     (Proto__DaosResponse *message,
                      ProtobufCAllocator *allocator);
/* --- per-message closures --- */

typedef void (*Proto__DaosRank_Closure)
                 (const Proto__DaosRank *message,
                  void *closure_data);
typedef void (*Proto__DaosResponse_Closure)
                 (const Proto__DaosResponse *message,
                  void *closure_data);

/* --- services --- */


/* --- descriptors --- */

extern const ProtobufCEnumDescriptor    proto__daos_request_status__descriptor;
extern const ProtobufCMessageDescriptor proto__daos_rank__descriptor;
extern const ProtobufCMessageDescriptor proto__daos_response__descriptor;

PROTOBUF_C__END_DECLS


#endif  /* PROTOBUF_C_srv_2eproto__INCLUDED */
