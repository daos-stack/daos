/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: DunsAttribute.proto */

/* Do not generate deprecated warnings for self */
#ifndef PROTOBUF_C__NO_DEPRECATED
#define PROTOBUF_C__NO_DEPRECATED
#endif

#include "DunsAttribute.pb-c.h"
void   uns__duns_attribute__init
                     (Uns__DunsAttribute         *message)
{
  static const Uns__DunsAttribute init_value = UNS__DUNS_ATTRIBUTE__INIT;
  *message = init_value;
}
size_t uns__duns_attribute__get_packed_size
                     (const Uns__DunsAttribute *message)
{
  assert(message->base.descriptor == &uns__duns_attribute__descriptor);
  return protobuf_c_message_get_packed_size ((const ProtobufCMessage*)(message));
}
size_t uns__duns_attribute__pack
                     (const Uns__DunsAttribute *message,
                      uint8_t       *out)
{
  assert(message->base.descriptor == &uns__duns_attribute__descriptor);
  return protobuf_c_message_pack ((const ProtobufCMessage*)message, out);
}
size_t uns__duns_attribute__pack_to_buffer
                     (const Uns__DunsAttribute *message,
                      ProtobufCBuffer *buffer)
{
  assert(message->base.descriptor == &uns__duns_attribute__descriptor);
  return protobuf_c_message_pack_to_buffer ((const ProtobufCMessage*)message, buffer);
}
Uns__DunsAttribute *
       uns__duns_attribute__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data)
{
  return (Uns__DunsAttribute *)
     protobuf_c_message_unpack (&uns__duns_attribute__descriptor,
                                allocator, len, data);
}
void   uns__duns_attribute__free_unpacked
                     (Uns__DunsAttribute *message,
                      ProtobufCAllocator *allocator)
{
  if(!message)
    return;
  assert(message->base.descriptor == &uns__duns_attribute__descriptor);
  protobuf_c_message_free_unpacked ((ProtobufCMessage*)message, allocator);
}
static const ProtobufCFieldDescriptor uns__duns_attribute__field_descriptors[8] =
{
  {
    "puuid",
    1,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Uns__DunsAttribute, puuid),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "cuuid",
    2,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Uns__DunsAttribute, cuuid),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "layout_type",
    3,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_ENUM,
    0,   /* quantifier_offset */
    offsetof(Uns__DunsAttribute, layout_type),
    &uns__layout__descriptor,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "object_type",
    4,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Uns__DunsAttribute, object_type),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "chunk_size",
    5,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_UINT64,
    0,   /* quantifier_offset */
    offsetof(Uns__DunsAttribute, chunk_size),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "rel_path",
    6,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_STRING,
    0,   /* quantifier_offset */
    offsetof(Uns__DunsAttribute, rel_path),
    NULL,
    &protobuf_c_empty_string,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "on_lustre",
    7,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_BOOL,
    0,   /* quantifier_offset */
    offsetof(Uns__DunsAttribute, on_lustre),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
  {
    "no_prefix",
    9,
    PROTOBUF_C_LABEL_NONE,
    PROTOBUF_C_TYPE_BOOL,
    0,   /* quantifier_offset */
    offsetof(Uns__DunsAttribute, no_prefix),
    NULL,
    NULL,
    0,             /* flags */
    0,NULL,NULL    /* reserved1,reserved2, etc */
  },
};
static const unsigned uns__duns_attribute__field_indices_by_name[] = {
  4,   /* field[4] = chunk_size */
  1,   /* field[1] = cuuid */
  2,   /* field[2] = layout_type */
  7,   /* field[7] = no_prefix */
  3,   /* field[3] = object_type */
  6,   /* field[6] = on_lustre */
  0,   /* field[0] = puuid */
  5,   /* field[5] = rel_path */
};
static const ProtobufCIntRange uns__duns_attribute__number_ranges[2 + 1] =
{
  { 1, 0 },
  { 9, 7 },
  { 0, 8 }
};
const ProtobufCMessageDescriptor uns__duns_attribute__descriptor =
{
  PROTOBUF_C__MESSAGE_DESCRIPTOR_MAGIC,
  "uns.DunsAttribute",
  "DunsAttribute",
  "Uns__DunsAttribute",
  "uns",
  sizeof(Uns__DunsAttribute),
  8,
  uns__duns_attribute__field_descriptors,
  uns__duns_attribute__field_indices_by_name,
  2,  uns__duns_attribute__number_ranges,
  (ProtobufCMessageInit) uns__duns_attribute__init,
  NULL,NULL,NULL    /* reserved[123] */
};
static const ProtobufCEnumValue uns__layout__enum_values_by_number[3] =
{
  { "UNKNOWN", "UNS__LAYOUT__UNKNOWN", 0 },
  { "POSIX", "UNS__LAYOUT__POSIX", 1 },
  { "HDF5", "UNS__LAYOUT__HDF5", 2 },
};
static const ProtobufCIntRange uns__layout__value_ranges[] = {
{0, 0},{0, 3}
};
static const ProtobufCEnumValueIndex uns__layout__enum_values_by_name[3] =
{
  { "HDF5", 2 },
  { "POSIX", 1 },
  { "UNKNOWN", 0 },
};
const ProtobufCEnumDescriptor uns__layout__descriptor =
{
  PROTOBUF_C__ENUM_DESCRIPTOR_MAGIC,
  "uns.Layout",
  "Layout",
  "Uns__Layout",
  "uns",
  3,
  uns__layout__enum_values_by_number,
  3,
  uns__layout__enum_values_by_name,
  1,
  uns__layout__value_ranges,
  NULL,NULL,NULL,NULL   /* reserved[1234] */
};
