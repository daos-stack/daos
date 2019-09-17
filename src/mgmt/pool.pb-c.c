/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: pool.proto */

/* Do not generate deprecated warnings for self */
#ifndef PROTOBUF_C__NO_DEPRECATED
#define PROTOBUF_C__NO_DEPRECATED
#endif

#include "pool.pb-c.h"
void   mgmt__pool_create_req__init
                     (Mgmt__PoolCreateReq         *message)
{
  static const Mgmt__PoolCreateReq init_value = MGMT__POOL_CREATE_REQ__INIT;
  *message = init_value;
}
size_t mgmt__pool_create_req__get_packed_size
                     (const Mgmt__PoolCreateReq *message)
{
  assert(message->base.descriptor == &mgmt__pool_create_req__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t mgmt__pool_create_req__pack
                     (const Mgmt__PoolCreateReq *message,
                      uint8_t       *out)
{
  assert(message->base.descriptor == &mgmt__pool_create_req__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t mgmt__pool_create_req__pack_to_buffer
                     (const Mgmt__PoolCreateReq *message,
                      ProtobufCBuffer *buffer)
{
  assert(message->base.descriptor == &mgmt__pool_create_req__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
Mgmt__PoolCreateReq *
       mgmt__pool_create_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (Mgmt__PoolCreateReq *)
     protobuf_c_message_unpack (&mgmt__pool_create_req__descriptor,
                                allocator, len, data);
}
void   mgmt__pool_create_req__free_unpacked
                     (Mgmt__PoolCreateReq *message,
                      ProtobufCAllocator *allocator)
{
  if(!message)
    return;
  assert(message->base.descriptor == &mgmt__pool_create_req__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
void   mgmt__pool_create_resp__init
                     (Mgmt__PoolCreateResp         *message)
{
  static const Mgmt__PoolCreateResp init_value = MGMT__POOL_CREATE_RESP__INIT;
  *message = init_value;
}
size_t mgmt__pool_create_resp__get_packed_size
                     (const Mgmt__PoolCreateResp *message)
{
  assert(message->base.descriptor == &mgmt__pool_create_resp__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t mgmt__pool_create_resp__pack
                     (const Mgmt__PoolCreateResp *message,
                      uint8_t       *out)
{
  assert(message->base.descriptor == &mgmt__pool_create_resp__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t mgmt__pool_create_resp__pack_to_buffer
                     (const Mgmt__PoolCreateResp *message,
                      ProtobufCBuffer *buffer)
{
  assert(message->base.descriptor == &mgmt__pool_create_resp__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
Mgmt__PoolCreateResp *
       mgmt__pool_create_resp__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (Mgmt__PoolCreateResp *)
     protobuf_c_message_unpack (&mgmt__pool_create_resp__descriptor,
                                allocator, len, data);
}
void   mgmt__pool_create_resp__free_unpacked
                     (Mgmt__PoolCreateResp *message,
                      ProtobufCAllocator *allocator)
{
  if(!message)
    return;
  assert(message->base.descriptor == &mgmt__pool_create_resp__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
void   mgmt__pool_destroy_req__init
                     (Mgmt__PoolDestroyReq         *message)
{
  static const Mgmt__PoolDestroyReq init_value = MGMT__POOL_DESTROY_REQ__INIT;
  *message = init_value;
}
size_t mgmt__pool_destroy_req__get_packed_size
                     (const Mgmt__PoolDestroyReq *message)
{
  assert(message->base.descriptor == &mgmt__pool_destroy_req__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t mgmt__pool_destroy_req__pack
                     (const Mgmt__PoolDestroyReq *message,
                      uint8_t       *out)
{
  assert(message->base.descriptor == &mgmt__pool_destroy_req__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t mgmt__pool_destroy_req__pack_to_buffer
                     (const Mgmt__PoolDestroyReq *message,
                      ProtobufCBuffer *buffer)
{
  assert(message->base.descriptor == &mgmt__pool_destroy_req__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
Mgmt__PoolDestroyReq *
       mgmt__pool_destroy_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (Mgmt__PoolDestroyReq *)
     protobuf_c_message_unpack (&mgmt__pool_destroy_req__descriptor,
                                allocator, len, data);
}
void   mgmt__pool_destroy_req__free_unpacked
                     (Mgmt__PoolDestroyReq *message,
                      ProtobufCAllocator *allocator)
{
  if(!message)
    return;
  assert(message->base.descriptor == &mgmt__pool_destroy_req__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
void   mgmt__pool_destroy_resp__init
                     (Mgmt__PoolDestroyResp         *message)
{
  static const Mgmt__PoolDestroyResp init_value = MGMT__POOL_DESTROY_RESP__INIT;
  *message = init_value;
}
size_t mgmt__pool_destroy_resp__get_packed_size
                     (const Mgmt__PoolDestroyResp *message)
{
  assert(message->base.descriptor == &mgmt__pool_destroy_resp__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t mgmt__pool_destroy_resp__pack
                     (const Mgmt__PoolDestroyResp *message,
                      uint8_t       *out)
{
  assert(message->base.descriptor == &mgmt__pool_destroy_resp__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t mgmt__pool_destroy_resp__pack_to_buffer
                     (const Mgmt__PoolDestroyResp *message,
                      ProtobufCBuffer *buffer)
{
  assert(message->base.descriptor == &mgmt__pool_destroy_resp__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
Mgmt__PoolDestroyResp *
       mgmt__pool_destroy_resp__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (Mgmt__PoolDestroyResp *)
     protobuf_c_message_unpack (&mgmt__pool_destroy_resp__descriptor,
                                allocator, len, data);
}
void   mgmt__pool_destroy_resp__free_unpacked
                     (Mgmt__PoolDestroyResp *message,
                      ProtobufCAllocator *allocator)
{
  if(!message)
    return;
  assert(message->base.descriptor == &mgmt__pool_destroy_resp__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
static const ProtobufCFieldDescriptor mgmt__pool_create_req__field_descriptors[7] =
{
  {
    "scmbytes",
    1,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_UINT64,
    0,   /* quantifier_offset */
    offsetof(Mgmt__PoolCreateReq, scmbytes),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "nvmebytes",
    2,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_UINT64,
    0,   /* quantifier_offset */
    offsetof(Mgmt__PoolCreateReq, nvmebytes),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "ranks",
    3,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Mgmt__PoolCreateReq, ranks),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "numsvcreps",
    4,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_UINT32,
    0,   /* quantifier_offset */
    offsetof(Mgmt__PoolCreateReq, numsvcreps),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "user",
    5,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Mgmt__PoolCreateReq, user),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "usergroup",
    6,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Mgmt__PoolCreateReq, usergroup),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "sys",
    7,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Mgmt__PoolCreateReq, sys),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned mgmt__pool_create_req__field_indices_by_name[] = {
  3,   /* field[3] = numsvcreps */
  1,   /* field[1] = nvmebytes */
  2,   /* field[2] = ranks */
  0,   /* field[0] = scmbytes */
  6,   /* field[6] = sys */
  4,   /* field[4] = user */
  5,   /* field[5] = usergroup */
};
static const ProtobufCIntRange mgmt__pool_create_req__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 7 }
};
const ProtobufCMessageDescriptor mgmt__pool_create_req__descriptor =
{
  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
  "mgmt.PoolCreateReq",
  "PoolCreateReq",
  "Mgmt__PoolCreateReq",
  "mgmt",
  sizeof(Mgmt__PoolCreateReq),
  7,
  mgmt__pool_create_req__field_descriptors,
  mgmt__pool_create_req__field_indices_by_name,
  1,  mgmt__pool_create_req__number_ranges,
  (ProtobufCMessageInit) mgmt__pool_create_req__init,
  NULL,NULL,NULL    /* reserved[123] */
};
static const ProtobufCFieldDescriptor mgmt__pool_create_resp__field_descriptors[3] =
{
  {
    "status",
    1,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_INT32,
    0,   /* quantifier_offset */
    offsetof(Mgmt__PoolCreateResp, status),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "uuid",
    2,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Mgmt__PoolCreateResp, uuid),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "svcreps",
    3,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Mgmt__PoolCreateResp, svcreps),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned mgmt__pool_create_resp__field_indices_by_name[] = {
  0,   /* field[0] = status */
  2,   /* field[2] = svcreps */
  1,   /* field[1] = uuid */
};
static const ProtobufCIntRange mgmt__pool_create_resp__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 3 }
};
const ProtobufCMessageDescriptor mgmt__pool_create_resp__descriptor =
{
  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
  "mgmt.PoolCreateResp",
  "PoolCreateResp",
  "Mgmt__PoolCreateResp",
  "mgmt",
  sizeof(Mgmt__PoolCreateResp),
  3,
  mgmt__pool_create_resp__field_descriptors,
  mgmt__pool_create_resp__field_indices_by_name,
  1,  mgmt__pool_create_resp__number_ranges,
  (ProtobufCMessageInit) mgmt__pool_create_resp__init,
  NULL,NULL,NULL    /* reserved[123] */
};
static const ProtobufCFieldDescriptor mgmt__pool_destroy_req__field_descriptors[3] =
{
  {
    "uuid",
    1,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Mgmt__PoolDestroyReq, uuid),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "sys",
    2,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Mgmt__PoolDestroyReq, sys),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "force",
    3,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_BOOL,
    0,   /* quantifier_offset */
    offsetof(Mgmt__PoolDestroyReq, force),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned mgmt__pool_destroy_req__field_indices_by_name[] = {
  2,   /* field[2] = force */
  1,   /* field[1] = sys */
  0,   /* field[0] = uuid */
};
static const ProtobufCIntRange mgmt__pool_destroy_req__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 3 }
};
const ProtobufCMessageDescriptor mgmt__pool_destroy_req__descriptor =
{
  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
  "mgmt.PoolDestroyReq",
  "PoolDestroyReq",
  "Mgmt__PoolDestroyReq",
  "mgmt",
  sizeof(Mgmt__PoolDestroyReq),
  3,
  mgmt__pool_destroy_req__field_descriptors,
  mgmt__pool_destroy_req__field_indices_by_name,
  1,  mgmt__pool_destroy_req__number_ranges,
  (ProtobufCMessageInit) mgmt__pool_destroy_req__init,
  NULL,NULL,NULL    /* reserved[123] */
};
static const ProtobufCFieldDescriptor mgmt__pool_destroy_resp__field_descriptors[1] =
{
  {
    "status",
    1,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_INT32,
    0,   /* quantifier_offset */
    offsetof(Mgmt__PoolDestroyResp, status),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned mgmt__pool_destroy_resp__field_indices_by_name[] = {
  0,   /* field[0] = status */
};
static const ProtobufCIntRange mgmt__pool_destroy_resp__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 1 }
};
const ProtobufCMessageDescriptor mgmt__pool_destroy_resp__descriptor =
{
  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
  "mgmt.PoolDestroyResp",
  "PoolDestroyResp",
  "Mgmt__PoolDestroyResp",
  "mgmt",
  sizeof(Mgmt__PoolDestroyResp),
  1,
  mgmt__pool_destroy_resp__field_descriptors,
  mgmt__pool_destroy_resp__field_indices_by_name,
  1,  mgmt__pool_destroy_resp__number_ranges,
  (ProtobufCMessageInit) mgmt__pool_destroy_resp__init,
  NULL,NULL,NULL    /* reserved[123] */
};
