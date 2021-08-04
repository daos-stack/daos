/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: server.proto */

#ifndef PROTOBUF_C_server_2eproto__INCLUDED
#define PROTOBUF_C_server_2eproto__INCLUDED

#include <protobuf-c/protobuf-c.h>

PROTOBUF_C__BEGIN_DECLS

#if PROTOBUF_C_VERSION_NUMBER < 1003000
# error This file was generated by a newer version of protoc-c which is incompatible with your libprotobuf-c headers. Please update your headers.
#elif 1003003 < PROTOBUF_C_MIN_COMPILER_VERSION
# error This file was generated by an older version of protoc-c which is incompatible with your libprotobuf-c headers. Please regenerate this file with a newer version of protoc-c.
#endif


typedef struct _Ctl__SetLogMasksReq Ctl__SetLogMasksReq;
typedef struct _Ctl__SetLogMasksResp Ctl__SetLogMasksResp;


/* --- enums --- */


/* --- messages --- */

/*
 * SetLogMasksReq provides parameters to set system-wide log masks.
 */
struct  _Ctl__SetLogMasksReq
{
  ProtobufCMessage base;
  /*
   * DAOS system name
   */
  char *sys;
  /*
   * set log masks for a set of facilities to a given level
   */
  char *masks;
};
#define CTL__SET_LOG_MASKS_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&ctl__set_log_masks_req__descriptor) \
    , (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string }


/*
 * SetEngineLogMasksResp returns results of attempts to set engine log masks.
 */
struct  _Ctl__SetLogMasksResp
{
  ProtobufCMessage base;
  /*
   * DAOS error code
   */
  int32_t status;
};
#define CTL__SET_LOG_MASKS_RESP__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&ctl__set_log_masks_resp__descriptor) \
    , 0 }


/* Ctl__SetLogMasksReq methods */
void   ctl__set_log_masks_req__init
                     (Ctl__SetLogMasksReq         *message);
size_t ctl__set_log_masks_req__get_packed_size
                     (const Ctl__SetLogMasksReq   *message);
size_t ctl__set_log_masks_req__pack
                     (const Ctl__SetLogMasksReq   *message,
                      uint8_t             *out);
size_t ctl__set_log_masks_req__pack_to_buffer
                     (const Ctl__SetLogMasksReq   *message,
                      ProtobufCBuffer     *buffer);
Ctl__SetLogMasksReq *
       ctl__set_log_masks_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   ctl__set_log_masks_req__free_unpacked
                     (Ctl__SetLogMasksReq *message,
                      ProtobufCAllocator *allocator);
/* Ctl__SetLogMasksResp methods */
void   ctl__set_log_masks_resp__init
                     (Ctl__SetLogMasksResp         *message);
size_t ctl__set_log_masks_resp__get_packed_size
                     (const Ctl__SetLogMasksResp   *message);
size_t ctl__set_log_masks_resp__pack
                     (const Ctl__SetLogMasksResp   *message,
                      uint8_t             *out);
size_t ctl__set_log_masks_resp__pack_to_buffer
                     (const Ctl__SetLogMasksResp   *message,
                      ProtobufCBuffer     *buffer);
Ctl__SetLogMasksResp *
       ctl__set_log_masks_resp__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   ctl__set_log_masks_resp__free_unpacked
                     (Ctl__SetLogMasksResp *message,
                      ProtobufCAllocator *allocator);
/* --- per-message closures --- */

typedef void (*Ctl__SetLogMasksReq_Closure)
                 (const Ctl__SetLogMasksReq *message,
                  void *closure_data);
typedef void (*Ctl__SetLogMasksResp_Closure)
                 (const Ctl__SetLogMasksResp *message,
                  void *closure_data);

/* --- services --- */


/* --- descriptors --- */

extern const ProtobufCMessageDescriptor ctl__set_log_masks_req__descriptor;
extern const ProtobufCMessageDescriptor ctl__set_log_masks_resp__descriptor;

PROTOBUF_C__END_DECLS


#endif  /* PROTOBUF_C_server_2eproto__INCLUDED */
