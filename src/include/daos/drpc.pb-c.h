/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: drpc.proto */

#ifndef PROTOBUF_C_drpc_2eproto__INCLUDED
#define PROTOBUF_C_drpc_2eproto__INCLUDED

#include <protobuf-c/protobuf-c.h>

PROTOBUF_C__BEGIN_DECLS

#if PROTOBUF_C_VERSION_NUMBER < 1003000
# error This file was generated by a newer version of protoc-c which is incompatible with your libprotobuf-c headers. Please update your headers.
#elif 1003000 < PROTOBUF_C_MIN_COMPILER_VERSION
# error This file was generated by an older version of protoc-c which is incompatible with your libprotobuf-c headers. Please regenerate this file with a newer version of protoc-c.
#endif


typedef struct _Drpc__Call Drpc__Call;
typedef struct _Drpc__Response Drpc__Response;


/* --- enums --- */

/*
 * Status represents the valid values for a response status.
 */
typedef enum _Drpc__Status {
  /*
   * The method executed and provided a response payload, if needed. Otherwise, the method simply succeeded.
   */
  DRPC__STATUS__SUCCESS = 0,
  /*
   * The method has been queued for asynchronous execution.
   */
  DRPC__STATUS__SUBMITTED = 1,
  /*
   * The method has failed and did not provide a response payload.
   */
  DRPC__STATUS__FAILURE = 2,
  /*
   * The requested module does not exist.
   */
  DRPC__STATUS__UNKNOWN_MODULE = 3,
  /*
   * The requested method does not exist.
   */
  DRPC__STATUS__UNKNOWN_METHOD = 4,
  /*
   * Could not unmarshal the incoming call.
   */
  DRPC__STATUS__FAILED_UNMARSHAL_CALL = 5,
  /*
   * Could not unmarshal the method-specific payload of the incoming call.
   */
  DRPC__STATUS__FAILED_UNMARSHAL_PAYLOAD = 6,
  /*
   * Generated a response payload, but couldn't marshal it into the response.
   */
  DRPC__STATUS__FAILED_MARSHAL = 7
    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(DRPC__STATUS)
} Drpc__Status;

/* --- messages --- */

/*
 * Call describes a function call to be executed over the dRPC channel.
 */
struct  _Drpc__Call
{
  ProtobufCMessage base;
  /*
   * ID of the module to process the call.
   */
  int32_t module;
  /*
   * ID of the method to be executed.
   */
  int32_t method;
  /*
   * Sequence number for matching a response to this call.
   */
  int64_t sequence;
  /*
   * Input payload to be used by the method.
   */
  ProtobufCBinaryData body;
};
#define DRPC__CALL__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&drpc__call__descriptor) \
    , 0, 0, 0, {0,NULL} }


/*
 * Response describes the result of a dRPC call.
 */
struct  _Drpc__Response
{
  ProtobufCMessage base;
  /*
   * Sequence number of the Call that triggered this response.
   */
  int64_t sequence;
  /*
   * High-level status of the RPC. If SUCCESS, method-specific status may be included in the body.
   */
  Drpc__Status status;
  /*
   * Output payload produced by the method.
   */
  ProtobufCBinaryData body;
};
#define DRPC__RESPONSE__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&drpc__response__descriptor) \
    , 0, DRPC__STATUS__SUCCESS, {0,NULL} }


/* Drpc__Call methods */
void   drpc__call__init
                     (Drpc__Call         *message);
size_t drpc__call__get_packed_size
                     (const Drpc__Call   *message);
size_t drpc__call__pack
                     (const Drpc__Call   *message,
                      uint8_t             *out);
size_t drpc__call__pack_to_buffer
                     (const Drpc__Call   *message,
                      ProtobufCBuffer     *buffer);
Drpc__Call *
       drpc__call__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   drpc__call__free_unpacked
                     (Drpc__Call *message,
                      ProtobufCAllocator *allocator);
/* Drpc__Response methods */
void   drpc__response__init
                     (Drpc__Response         *message);
size_t drpc__response__get_packed_size
                     (const Drpc__Response   *message);
size_t drpc__response__pack
                     (const Drpc__Response   *message,
                      uint8_t             *out);
size_t drpc__response__pack_to_buffer
                     (const Drpc__Response   *message,
                      ProtobufCBuffer     *buffer);
Drpc__Response *
       drpc__response__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   drpc__response__free_unpacked
                     (Drpc__Response *message,
                      ProtobufCAllocator *allocator);
/* --- per-message closures --- */

typedef void (*Drpc__Call_Closure)
                 (const Drpc__Call *message,
                  void *closure_data);
typedef void (*Drpc__Response_Closure)
                 (const Drpc__Response *message,
                  void *closure_data);

/* --- services --- */


/* --- descriptors --- */

extern const ProtobufCEnumDescriptor    drpc__status__descriptor;
extern const ProtobufCMessageDescriptor drpc__call__descriptor;
extern const ProtobufCMessageDescriptor drpc__response__descriptor;

PROTOBUF_C__END_DECLS


#endif  /* PROTOBUF_C_drpc_2eproto__INCLUDED */
