/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: srv.proto */

/* Do not generate deprecated warnings for self */
#ifndef PROTOBUF_C__NO_DEPRECATED
#define PROTOBUF_C__NO_DEPRECATED
#endif

#include "srv.pb-c.h"
void   srv__notify_ready_req__init
                     (Srv__NotifyReadyReq         *message)
{
  static const Srv__NotifyReadyReq init_value = SRV__NOTIFY_READY_REQ__INIT;
  *message = init_value;
}
size_t srv__notify_ready_req__get_packed_size
                     (const Srv__NotifyReadyReq *message)
{
  assert(message->base.descriptor == &srv__notify_ready_req__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t srv__notify_ready_req__pack
                     (const Srv__NotifyReadyReq *message,
                      uint8_t       *out)
{
  assert(message->base.descriptor == &srv__notify_ready_req__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t srv__notify_ready_req__pack_to_buffer
                     (const Srv__NotifyReadyReq *message,
                      ProtobufCBuffer *buffer)
{
  assert(message->base.descriptor == &srv__notify_ready_req__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
Srv__NotifyReadyReq *
       srv__notify_ready_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (Srv__NotifyReadyReq *)
     protobuf_c_message_unpack (&srv__notify_ready_req__descriptor,
                                allocator, len, data);
}
void   srv__notify_ready_req__free_unpacked
                     (Srv__NotifyReadyReq *message,
                      ProtobufCAllocator *allocator)
{
  if(!message)
    return;
  assert(message->base.descriptor == &srv__notify_ready_req__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
void   srv__bio_error_req__init
                     (Srv__BioErrorReq         *message)
{
  static const Srv__BioErrorReq init_value = SRV__BIO_ERROR_REQ__INIT;
  *message = init_value;
}
size_t srv__bio_error_req__get_packed_size
                     (const Srv__BioErrorReq *message)
{
  assert(message->base.descriptor == &srv__bio_error_req__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t srv__bio_error_req__pack
                     (const Srv__BioErrorReq *message,
                      uint8_t       *out)
{
  assert(message->base.descriptor == &srv__bio_error_req__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t srv__bio_error_req__pack_to_buffer
                     (const Srv__BioErrorReq *message,
                      ProtobufCBuffer *buffer)
{
  assert(message->base.descriptor == &srv__bio_error_req__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
Srv__BioErrorReq *
       srv__bio_error_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (Srv__BioErrorReq *)
     protobuf_c_message_unpack (&srv__bio_error_req__descriptor,
                                allocator, len, data);
}
void   srv__bio_error_req__free_unpacked
                     (Srv__BioErrorReq *message,
                      ProtobufCAllocator *allocator)
{
  if(!message)
    return;
  assert(message->base.descriptor == &srv__bio_error_req__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
static const ProtobufCFieldDescriptor srv__notify_ready_req__field_descriptors[5] =
{
  {
    "uri",
    1,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Srv__NotifyReadyReq, uri),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "nctxs",
    2,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_UINT32,
    0,   /* quantifier_offset */
    offsetof(Srv__NotifyReadyReq, nctxs),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "drpcListenerSock",
    3,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Srv__NotifyReadyReq, drpclistenersock),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "instanceIdx",
    4,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_UINT32,
    0,   /* quantifier_offset */
    offsetof(Srv__NotifyReadyReq, instanceidx),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "ntgts",
    5,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_UINT32,
    0,   /* quantifier_offset */
    offsetof(Srv__NotifyReadyReq, ntgts),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned srv__notify_ready_req__field_indices_by_name[] = {
  2,   /* field[2] = drpcListenerSock */
  3,   /* field[3] = instanceIdx */
  1,   /* field[1] = nctxs */
  4,   /* field[4] = ntgts */
  0,   /* field[0] = uri */
};
static const ProtobufCIntRange srv__notify_ready_req__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 5 }
};
const ProtobufCMessageDescriptor srv__notify_ready_req__descriptor =
{
  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
  "srv.NotifyReadyReq",
  "NotifyReadyReq",
  "Srv__NotifyReadyReq",
  "srv",
  sizeof(Srv__NotifyReadyReq),
  5,
  srv__notify_ready_req__field_descriptors,
  srv__notify_ready_req__field_indices_by_name,
  1,  srv__notify_ready_req__number_ranges,
  (ProtobufCMessageInit) srv__notify_ready_req__init,
  NULL,NULL,NULL    /* reserved[123] */
};
static const ProtobufCFieldDescriptor srv__bio_error_req__field_descriptors[7] =
{
  {
    "unmapErr",
    1,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_BOOL,
    0,   /* quantifier_offset */
    offsetof(Srv__BioErrorReq, unmaperr),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "readErr",
    2,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_BOOL,
    0,   /* quantifier_offset */
    offsetof(Srv__BioErrorReq, readerr),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "writeErr",
    3,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_BOOL,
    0,   /* quantifier_offset */
    offsetof(Srv__BioErrorReq, writeerr),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "tgtId",
    4,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_INT32,
    0,   /* quantifier_offset */
    offsetof(Srv__BioErrorReq, tgtid),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "instanceIdx",
    5,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_UINT32,
    0,   /* quantifier_offset */
    offsetof(Srv__BioErrorReq, instanceidx),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "drpcListenerSock",
    6,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Srv__BioErrorReq, drpclistenersock),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "uri",
    7,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Srv__BioErrorReq, uri),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned srv__bio_error_req__field_indices_by_name[] = {
  5,   /* field[5] = drpcListenerSock */
  4,   /* field[4] = instanceIdx */
  1,   /* field[1] = readErr */
  3,   /* field[3] = tgtId */
  0,   /* field[0] = unmapErr */
  6,   /* field[6] = uri */
  2,   /* field[2] = writeErr */
};
static const ProtobufCIntRange srv__bio_error_req__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 7 }
};
const ProtobufCMessageDescriptor srv__bio_error_req__descriptor =
{
  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
  "srv.BioErrorReq",
  "BioErrorReq",
  "Srv__BioErrorReq",
  "srv",
  sizeof(Srv__BioErrorReq),
  7,
  srv__bio_error_req__field_descriptors,
  srv__bio_error_req__field_indices_by_name,
  1,  srv__bio_error_req__number_ranges,
  (ProtobufCMessageInit) srv__bio_error_req__init,
  NULL,NULL,NULL    /* reserved[123] */
};
