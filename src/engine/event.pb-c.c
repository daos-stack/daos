/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: event.proto */

/* Do not generate deprecated warnings for self */
#ifndef PROTOBUF_C__NO_DEPRECATED
#define PROTOBUF_C__NO_DEPRECATED
#endif

#include "event.pb-c.h"
void   shared__rasevent__engine_state_event_info__init
                     (Shared__RASEvent__EngineStateEventInfo         *message)
{
  static const Shared__RASEvent__EngineStateEventInfo init_value = SHARED__RASEVENT__ENGINE_STATE_EVENT_INFO__INIT;
  *message = init_value;
}
void   shared__rasevent__pool_svc_event_info__init
                     (Shared__RASEvent__PoolSvcEventInfo         *message)
{
  static const Shared__RASEvent__PoolSvcEventInfo init_value = SHARED__RASEVENT__POOL_SVC_EVENT_INFO__INIT;
  *message = init_value;
}
void   shared__rasevent__init
                     (Shared__RASEvent         *message)
{
  static const Shared__RASEvent init_value = SHARED__RASEVENT__INIT;
  *message = init_value;
}
size_t shared__rasevent__get_packed_size
                     (const Shared__RASEvent *message)
{
  assert(message->base.descriptor == &shared__rasevent__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t shared__rasevent__pack
                     (const Shared__RASEvent *message,
                      uint8_t       *out)
{
  assert(message->base.descriptor == &shared__rasevent__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t shared__rasevent__pack_to_buffer
                     (const Shared__RASEvent *message,
                      ProtobufCBuffer *buffer)
{
  assert(message->base.descriptor == &shared__rasevent__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
Shared__RASEvent *
       shared__rasevent__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (Shared__RASEvent *)
     protobuf_c_message_unpack (&shared__rasevent__descriptor,
                                allocator, len, data);
}
void   shared__rasevent__free_unpacked
                     (Shared__RASEvent *message,
                      ProtobufCAllocator *allocator)
{
  if(!message)
    return;
  assert(message->base.descriptor == &shared__rasevent__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
void   shared__cluster_event_req__init
                     (Shared__ClusterEventReq         *message)
{
  static const Shared__ClusterEventReq init_value = SHARED__CLUSTER_EVENT_REQ__INIT;
  *message = init_value;
}
size_t shared__cluster_event_req__get_packed_size
                     (const Shared__ClusterEventReq *message)
{
  assert(message->base.descriptor == &shared__cluster_event_req__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t shared__cluster_event_req__pack
                     (const Shared__ClusterEventReq *message,
                      uint8_t       *out)
{
  assert(message->base.descriptor == &shared__cluster_event_req__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t shared__cluster_event_req__pack_to_buffer
                     (const Shared__ClusterEventReq *message,
                      ProtobufCBuffer *buffer)
{
  assert(message->base.descriptor == &shared__cluster_event_req__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
Shared__ClusterEventReq *
       shared__cluster_event_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (Shared__ClusterEventReq *)
     protobuf_c_message_unpack (&shared__cluster_event_req__descriptor,
                                allocator, len, data);
}
void   shared__cluster_event_req__free_unpacked
                     (Shared__ClusterEventReq *message,
                      ProtobufCAllocator *allocator)
{
  if(!message)
    return;
  assert(message->base.descriptor == &shared__cluster_event_req__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
void   shared__cluster_event_resp__init
                     (Shared__ClusterEventResp         *message)
{
  static const Shared__ClusterEventResp init_value = SHARED__CLUSTER_EVENT_RESP__INIT;
  *message = init_value;
}
size_t shared__cluster_event_resp__get_packed_size
                     (const Shared__ClusterEventResp *message)
{
  assert(message->base.descriptor == &shared__cluster_event_resp__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t shared__cluster_event_resp__pack
                     (const Shared__ClusterEventResp *message,
                      uint8_t       *out)
{
  assert(message->base.descriptor == &shared__cluster_event_resp__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t shared__cluster_event_resp__pack_to_buffer
                     (const Shared__ClusterEventResp *message,
                      ProtobufCBuffer *buffer)
{
  assert(message->base.descriptor == &shared__cluster_event_resp__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
Shared__ClusterEventResp *
       shared__cluster_event_resp__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (Shared__ClusterEventResp *)
     protobuf_c_message_unpack (&shared__cluster_event_resp__descriptor,
                                allocator, len, data);
}
void   shared__cluster_event_resp__free_unpacked
                     (Shared__ClusterEventResp *message,
                      ProtobufCAllocator *allocator)
{
  if(!message)
    return;
  assert(message->base.descriptor == &shared__cluster_event_resp__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
static const ProtobufCFieldDescriptor shared__rasevent__engine_state_event_info__field_descriptors[3] =
{
  {
    "instance",
    1,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_UINT32,
    0,   /* quantifier_offset */
    offsetof(Shared__RASEvent__EngineStateEventInfo, instance),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "errored",
    2,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_BOOL,
    0,   /* quantifier_offset */
    offsetof(Shared__RASEvent__EngineStateEventInfo, errored),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "error",
    3,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Shared__RASEvent__EngineStateEventInfo, error),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned shared__rasevent__engine_state_event_info__field_indices_by_name[] = {
  2,   /* field[2] = error */
  1,   /* field[1] = errored */
  0,   /* field[0] = instance */
};
static const ProtobufCIntRange shared__rasevent__engine_state_event_info__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 3 }
};
const ProtobufCMessageDescriptor shared__rasevent__engine_state_event_info__descriptor =
{
  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
  "shared.RASEvent.EngineStateEventInfo",
  "EngineStateEventInfo",
  "Shared__RASEvent__EngineStateEventInfo",
  "shared",
  sizeof(Shared__RASEvent__EngineStateEventInfo),
  3,
  shared__rasevent__engine_state_event_info__field_descriptors,
  shared__rasevent__engine_state_event_info__field_indices_by_name,
  1,  shared__rasevent__engine_state_event_info__number_ranges,
  (ProtobufCMessageInit) shared__rasevent__engine_state_event_info__init,
  NULL,NULL,NULL    /* reserved[123] */
};
static const ProtobufCFieldDescriptor shared__rasevent__pool_svc_event_info__field_descriptors[2] =
{
  {
    "svc_reps",
    1,
    PROTOBUF_C_LABEL_REPEATED,
    PROTOBUF_C_TYPE_UINT32,
    offsetof(Shared__RASEvent__PoolSvcEventInfo, n_svc_reps),
    offsetof(Shared__RASEvent__PoolSvcEventInfo, svc_reps),
    NULL,
    NULL,
    0 | PROTOBUF_C_FIELD_FLAG_PACKED,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "version",
    2,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_UINT64,
    0,   /* quantifier_offset */
    offsetof(Shared__RASEvent__PoolSvcEventInfo, version),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned shared__rasevent__pool_svc_event_info__field_indices_by_name[] = {
  0,   /* field[0] = svc_reps */
  1,   /* field[1] = version */
};
static const ProtobufCIntRange shared__rasevent__pool_svc_event_info__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 2 }
};
const ProtobufCMessageDescriptor shared__rasevent__pool_svc_event_info__descriptor =
{
  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
  "shared.RASEvent.PoolSvcEventInfo",
  "PoolSvcEventInfo",
  "Shared__RASEvent__PoolSvcEventInfo",
  "shared",
  sizeof(Shared__RASEvent__PoolSvcEventInfo),
  2,
  shared__rasevent__pool_svc_event_info__field_descriptors,
  shared__rasevent__pool_svc_event_info__field_indices_by_name,
  1,  shared__rasevent__pool_svc_event_info__number_ranges,
  (ProtobufCMessageInit) shared__rasevent__pool_svc_event_info__init,
  NULL,NULL,NULL    /* reserved[123] */
};
static const ProtobufCFieldDescriptor shared__rasevent__field_descriptors[19] =
{
  {
    "id",
    1,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_UINT32,
    0,   /* quantifier_offset */
    offsetof(Shared__RASEvent, id),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "msg",
    2,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Shared__RASEvent, msg),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "timestamp",
    3,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Shared__RASEvent, timestamp),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "type",
    4,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_UINT32,
    0,   /* quantifier_offset */
    offsetof(Shared__RASEvent, type),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "severity",
    5,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_UINT32,
    0,   /* quantifier_offset */
    offsetof(Shared__RASEvent, severity),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "hostname",
    6,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Shared__RASEvent, hostname),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "rank",
    7,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_UINT32,
    0,   /* quantifier_offset */
    offsetof(Shared__RASEvent, rank),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "incarnation",
    8,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_UINT64,
    0,   /* quantifier_offset */
    offsetof(Shared__RASEvent, incarnation),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "hw_id",
    9,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Shared__RASEvent, hw_id),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "proc_id",
    10,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_UINT64,
    0,   /* quantifier_offset */
    offsetof(Shared__RASEvent, proc_id),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "thread_id",
    11,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_UINT64,
    0,   /* quantifier_offset */
    offsetof(Shared__RASEvent, thread_id),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "job_id",
    12,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Shared__RASEvent, job_id),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "pool_uuid",
    13,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Shared__RASEvent, pool_uuid),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "cont_uuid",
    14,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Shared__RASEvent, cont_uuid),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "obj_id",
    15,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Shared__RASEvent, obj_id),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "ctl_op",
    16,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Shared__RASEvent, ctl_op),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "str_info",
    17,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    offsetof(Shared__RASEvent, extended_info_case),
    offsetof(Shared__RASEvent, str_info),
    NULL,
    &protobuf_c_empty_string,
    0 | PROTOBUF_C_FIELD_FLAG_ONEOF,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "engine_state_info",
    18,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_MESSAGE,
    offsetof(Shared__RASEvent, extended_info_case),
    offsetof(Shared__RASEvent, engine_state_info),
    &shared__rasevent__engine_state_event_info__descriptor,
    NULL,
    0 | PROTOBUF_C_FIELD_FLAG_ONEOF,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "pool_svc_info",
    19,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_MESSAGE,
    offsetof(Shared__RASEvent, extended_info_case),
    offsetof(Shared__RASEvent, pool_svc_info),
    &shared__rasevent__pool_svc_event_info__descriptor,
    NULL,
    0 | PROTOBUF_C_FIELD_FLAG_ONEOF,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned shared__rasevent__field_indices_by_name[] = {
  13,   /* field[13] = cont_uuid */
  15,   /* field[15] = ctl_op */
  17,   /* field[17] = engine_state_info */
  5,   /* field[5] = hostname */
  8,   /* field[8] = hw_id */
  0,   /* field[0] = id */
  7,   /* field[7] = incarnation */
  11,   /* field[11] = job_id */
  1,   /* field[1] = msg */
  14,   /* field[14] = obj_id */
  18,   /* field[18] = pool_svc_info */
  12,   /* field[12] = pool_uuid */
  9,   /* field[9] = proc_id */
  6,   /* field[6] = rank */
  4,   /* field[4] = severity */
  16,   /* field[16] = str_info */
  10,   /* field[10] = thread_id */
  2,   /* field[2] = timestamp */
  3,   /* field[3] = type */
};
static const ProtobufCIntRange shared__rasevent__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 19 }
};
const ProtobufCMessageDescriptor shared__rasevent__descriptor =
{
  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
  "shared.RASEvent",
  "RASEvent",
  "Shared__RASEvent",
  "shared",
  sizeof(Shared__RASEvent),
  19,
  shared__rasevent__field_descriptors,
  shared__rasevent__field_indices_by_name,
  1,  shared__rasevent__number_ranges,
  (ProtobufCMessageInit) shared__rasevent__init,
  NULL,NULL,NULL    /* reserved[123] */
};
static const ProtobufCFieldDescriptor shared__cluster_event_req__field_descriptors[2] =
{
  {
    "sequence",
    1,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_UINT64,
    0,   /* quantifier_offset */
    offsetof(Shared__ClusterEventReq, sequence),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "event",
    2,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_MESSAGE,
    0,   /* quantifier_offset */
    offsetof(Shared__ClusterEventReq, event),
    &shared__rasevent__descriptor,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned shared__cluster_event_req__field_indices_by_name[] = {
  1,   /* field[1] = event */
  0,   /* field[0] = sequence */
};
static const ProtobufCIntRange shared__cluster_event_req__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 2 }
};
const ProtobufCMessageDescriptor shared__cluster_event_req__descriptor =
{
  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
  "shared.ClusterEventReq",
  "ClusterEventReq",
  "Shared__ClusterEventReq",
  "shared",
  sizeof(Shared__ClusterEventReq),
  2,
  shared__cluster_event_req__field_descriptors,
  shared__cluster_event_req__field_indices_by_name,
  1,  shared__cluster_event_req__number_ranges,
  (ProtobufCMessageInit) shared__cluster_event_req__init,
  NULL,NULL,NULL    /* reserved[123] */
};
static const ProtobufCFieldDescriptor shared__cluster_event_resp__field_descriptors[2] =
{
  {
    "sequence",
    1,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_UINT64,
    0,   /* quantifier_offset */
    offsetof(Shared__ClusterEventResp, sequence),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "status",
    2,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_INT32,
    0,   /* quantifier_offset */
    offsetof(Shared__ClusterEventResp, status),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned shared__cluster_event_resp__field_indices_by_name[] = {
  0,   /* field[0] = sequence */
  1,   /* field[1] = status */
};
static const ProtobufCIntRange shared__cluster_event_resp__number_ranges[1 + 1] =
{
  { 1, 0 },
  { 0, 2 }
};
const ProtobufCMessageDescriptor shared__cluster_event_resp__descriptor =
{
  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
  "shared.ClusterEventResp",
  "ClusterEventResp",
  "Shared__ClusterEventResp",
  "shared",
  sizeof(Shared__ClusterEventResp),
  2,
  shared__cluster_event_resp__field_descriptors,
  shared__cluster_event_resp__field_indices_by_name,
  1,  shared__cluster_event_resp__number_ranges,
  (ProtobufCMessageInit) shared__cluster_event_resp__init,
  NULL,NULL,NULL    /* reserved[123] */
};
