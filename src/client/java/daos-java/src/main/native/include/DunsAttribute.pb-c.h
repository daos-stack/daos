/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: DunsAttribute.proto */

#ifndef PROTOBUF_C_DunsAttribute_2eproto__INCLUDED
#define PROTOBUF_C_DunsAttribute_2eproto__INCLUDED

#include <protobuf-c/protobuf-c.h>

PROTOBUF_C__BEGIN_DECLS

#if PROTOBUF_C_VERSION_NUMBER < 1003000
# error This file was generated by a newer version of protoc-c which is incompatible with your libprotobuf-c headers. Please update your headers.
#elif 1003003 < PROTOBUF_C_MIN_COMPILER_VERSION
# error This file was generated by an older version of protoc-c which is incompatible with your libprotobuf-c headers. Please regenerate this file with a newer version of protoc-c.
#endif


typedef struct _Uns__DaosAce Uns__DaosAce;
typedef struct _Uns__DaosAcl Uns__DaosAcl;
typedef struct _Uns__Entry Uns__Entry;
typedef struct _Uns__Properties Uns__Properties;
typedef struct _Uns__DunsAttribute Uns__DunsAttribute;


/* --- enums --- */

/*
 * property types of pool and container 
 */
typedef enum _Uns__PropType {
  /*
   * pool property types 
   */
  UNS__PROP_TYPE__DAOS_PROP_PO_MIN = 0,
  /*
   **
   * Label - a string that a user can associated with a pool.
   * default = ""
   */
  UNS__PROP_TYPE__DAOS_PROP_PO_LABEL = 1,
  /*
   **
   * ACL: access control list for pool
   * An ordered list of access control entries detailing user and group
   * access privileges.
   * Expected to be in the order: Owner, User(s), Group(s), Everyone
   */
  UNS__PROP_TYPE__DAOS_PROP_PO_ACL = 2,
  /*
   **
   * Reserve space ratio: amount of space to be reserved on each target
   * for rebuild purpose. default = 0%.
   */
  UNS__PROP_TYPE__DAOS_PROP_PO_SPACE_RB = 3,
  /*
   **
   * Automatic/manual self-healing. default = auto
   * auto/manual exclusion
   * auto/manual rebuild
   */
  UNS__PROP_TYPE__DAOS_PROP_PO_SELF_HEAL = 4,
  /*
   **
   * Space reclaim strategy = time|batched|snapshot. default = snapshot
   * time interval
   * batched commits
   * snapshot creation
   */
  UNS__PROP_TYPE__DAOS_PROP_PO_RECLAIM = 5,
  /*
   **
   * The user who acts as the owner of the pool.
   * Format: user@[domain]
   */
  UNS__PROP_TYPE__DAOS_PROP_PO_OWNER = 6,
  /*
   **
   * The group that acts as the owner of the pool.
   * Format: group@[domain]
   */
  UNS__PROP_TYPE__DAOS_PROP_PO_OWNER_GROUP = 7,
  /*
   **
   * The pool svc rank list.
   */
  UNS__PROP_TYPE__DAOS_PROP_PO_SVC_LIST = 8,
  UNS__PROP_TYPE__DAOS_PROP_PO_MAX = 9,
  /*
   * container property types 
   */
  UNS__PROP_TYPE__DAOS_PROP_CO_MIN = 4096,
  /*
   **
   * Label - a string that a user can associated with a container.
   * default = ""
   */
  UNS__PROP_TYPE__DAOS_PROP_CO_LABEL = 4097,
  /*
   **
   * Layout type: unknown, POSIX, MPI-IO, HDF5, Apache Arrow, ...
   * default value = DAOS_PROP_CO_LAYOUT_UNKOWN
   */
  UNS__PROP_TYPE__DAOS_PROP_CO_LAYOUT_TYPE = 4098,
  /*
   **
   * Layout version: specific to middleware for interop.
   * default = 1
   */
  UNS__PROP_TYPE__DAOS_PROP_CO_LAYOUT_VER = 4099,
  /*
   **
   * Checksum on/off + checksum type (CRC16, CRC32, SHA-1 & SHA-2).
   * default = DAOS_PROP_CO_CSUM_OFF
   */
  UNS__PROP_TYPE__DAOS_PROP_CO_CSUM = 4100,
  /*
   **
   * Checksum chunk size
   * default = 32K
   */
  UNS__PROP_TYPE__DAOS_PROP_CO_CSUM_CHUNK_SIZE = 4101,
  /*
   **
   * Checksum verification on server. Value = ON/OFF
   * default = DAOS_PROP_CO_CSUM_SV_OFF
   */
  UNS__PROP_TYPE__DAOS_PROP_CO_CSUM_SERVER_VERIFY = 4102,
  /*
   **
   * Redundancy factor:
   * RF(n): Container I/O restricted after n faults.
   * default = RF1 (DAOS_PROP_CO_REDUN_RF1)
   */
  UNS__PROP_TYPE__DAOS_PROP_CO_REDUN_FAC = 4103,
  /*
   **
   * Redundancy level: default fault domain level for placement.
   * default = rack (DAOS_PROP_CO_REDUN_NODE)
   */
  UNS__PROP_TYPE__DAOS_PROP_CO_REDUN_LVL = 4104,
  /*
   **
   * Maximum number of snapshots to retain.
   */
  UNS__PROP_TYPE__DAOS_PROP_CO_SNAPSHOT_MAX = 4105,
  /*
   **
   * ACL: access control list for container
   * An ordered list of access control entries detailing user and group
   * access privileges.
   * Expected to be in the order: Owner, User(s), Group(s), Everyone
   */
  UNS__PROP_TYPE__DAOS_PROP_CO_ACL = 4106,
  /*
   ** Compression on/off + compression type 
   */
  UNS__PROP_TYPE__DAOS_PROP_CO_COMPRESS = 4107,
  /*
   ** Encryption on/off + encryption type 
   */
  UNS__PROP_TYPE__DAOS_PROP_CO_ENCRYPT = 4108,
  /*
   **
   * The user who acts as the owner of the container.
   * Format: user@[domain]
   */
  UNS__PROP_TYPE__DAOS_PROP_CO_OWNER = 4109,
  /*
   **
   * The group that acts as the owner of the container.
   * Format: group@[domain]
   */
  UNS__PROP_TYPE__DAOS_PROP_CO_OWNER_GROUP = 4110,
  UNS__PROP_TYPE__DAOS_PROP_CO_MAX = 4111
    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(UNS__PROP_TYPE)
} Uns__PropType;
typedef enum _Uns__Layout {
  UNS__LAYOUT__UNKNOWN = 0,
  UNS__LAYOUT__POSIX = 1,
  UNS__LAYOUT__HDF5 = 2
    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(UNS__LAYOUT)
} Uns__Layout;

/* --- messages --- */

struct  _Uns__DaosAce
{
  ProtobufCMessage base;
  uint32_t access_types;
  uint32_t principal_type;
  uint32_t principal_len;
  uint32_t access_flags;
  uint32_t reserved;
  uint32_t allow_perms;
  uint32_t audit_perms;
  uint32_t alarm_perms;
  char *principal;
};
#define UNS__DAOS_ACE__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&uns__daos_ace__descriptor) \
    , 0, 0, 0, 0, 0, 0, 0, 0, (char *)protobuf_c_empty_string }


struct  _Uns__DaosAcl
{
  ProtobufCMessage base;
  uint32_t ver;
  uint32_t reserv;
  size_t n_aces;
  Uns__DaosAce **aces;
};
#define UNS__DAOS_ACL__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&uns__daos_acl__descriptor) \
    , 0, 0, 0,NULL }


typedef enum {
  UNS__ENTRY__VALUE__NOT_SET = 0,
  UNS__ENTRY__VALUE_VAL = 3,
  UNS__ENTRY__VALUE_STR = 4,
  UNS__ENTRY__VALUE_PVAL = 5
    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(UNS__ENTRY__VALUE)
} Uns__Entry__ValueCase;

struct  _Uns__Entry
{
  ProtobufCMessage base;
  Uns__PropType type;
  uint32_t reserved;
  Uns__Entry__ValueCase value_case;
  union {
    uint64_t val;
    char *str;
    Uns__DaosAcl *pval;
  };
};
#define UNS__ENTRY__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&uns__entry__descriptor) \
    , UNS__PROP_TYPE__DAOS_PROP_PO_MIN, 0, UNS__ENTRY__VALUE__NOT_SET, {0} }


struct  _Uns__Properties
{
  ProtobufCMessage base;
  uint32_t reserved;
  size_t n_entries;
  Uns__Entry **entries;
};
#define UNS__PROPERTIES__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&uns__properties__descriptor) \
    , 0, 0,NULL }


struct  _Uns__DunsAttribute
{
  ProtobufCMessage base;
  char *puuid;
  char *cuuid;
  Uns__Layout layout_type;
  char *object_type;
  uint64_t chunk_size;
  char *rel_path;
  protobuf_c_boolean on_lustre;
  Uns__Properties *properties;
  protobuf_c_boolean no_prefix;
};
#define UNS__DUNS_ATTRIBUTE__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&uns__duns_attribute__descriptor) \
    , (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, UNS__LAYOUT__UNKNOWN, (char *)protobuf_c_empty_string, 0, (char *)protobuf_c_empty_string, 0, NULL, 0 }


/* Uns__DaosAce methods */
void   uns__daos_ace__init
                     (Uns__DaosAce         *message);
size_t uns__daos_ace__get_packed_size
                     (const Uns__DaosAce   *message);
size_t uns__daos_ace__pack
                     (const Uns__DaosAce   *message,
                      uint8_t             *out);
size_t uns__daos_ace__pack_to_buffer
                     (const Uns__DaosAce   *message,
                      ProtobufCBuffer     *buffer);
Uns__DaosAce *
       uns__daos_ace__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   uns__daos_ace__free_unpacked
                     (Uns__DaosAce *message,
                      ProtobufCAllocator *allocator);
/* Uns__DaosAcl methods */
void   uns__daos_acl__init
                     (Uns__DaosAcl         *message);
size_t uns__daos_acl__get_packed_size
                     (const Uns__DaosAcl   *message);
size_t uns__daos_acl__pack
                     (const Uns__DaosAcl   *message,
                      uint8_t             *out);
size_t uns__daos_acl__pack_to_buffer
                     (const Uns__DaosAcl   *message,
                      ProtobufCBuffer     *buffer);
Uns__DaosAcl *
       uns__daos_acl__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   uns__daos_acl__free_unpacked
                     (Uns__DaosAcl *message,
                      ProtobufCAllocator *allocator);
/* Uns__Entry methods */
void   uns__entry__init
                     (Uns__Entry         *message);
size_t uns__entry__get_packed_size
                     (const Uns__Entry   *message);
size_t uns__entry__pack
                     (const Uns__Entry   *message,
                      uint8_t             *out);
size_t uns__entry__pack_to_buffer
                     (const Uns__Entry   *message,
                      ProtobufCBuffer     *buffer);
Uns__Entry *
       uns__entry__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   uns__entry__free_unpacked
                     (Uns__Entry *message,
                      ProtobufCAllocator *allocator);
/* Uns__Properties methods */
void   uns__properties__init
                     (Uns__Properties         *message);
size_t uns__properties__get_packed_size
                     (const Uns__Properties   *message);
size_t uns__properties__pack
                     (const Uns__Properties   *message,
                      uint8_t             *out);
size_t uns__properties__pack_to_buffer
                     (const Uns__Properties   *message,
                      ProtobufCBuffer     *buffer);
Uns__Properties *
       uns__properties__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   uns__properties__free_unpacked
                     (Uns__Properties *message,
                      ProtobufCAllocator *allocator);
/* Uns__DunsAttribute methods */
void   uns__duns_attribute__init
                     (Uns__DunsAttribute         *message);
size_t uns__duns_attribute__get_packed_size
                     (const Uns__DunsAttribute   *message);
size_t uns__duns_attribute__pack
                     (const Uns__DunsAttribute   *message,
                      uint8_t             *out);
size_t uns__duns_attribute__pack_to_buffer
                     (const Uns__DunsAttribute   *message,
                      ProtobufCBuffer     *buffer);
Uns__DunsAttribute *
       uns__duns_attribute__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   uns__duns_attribute__free_unpacked
                     (Uns__DunsAttribute *message,
                      ProtobufCAllocator *allocator);
/* --- per-message closures --- */

typedef void (*Uns__DaosAce_Closure)
                 (const Uns__DaosAce *message,
                  void *closure_data);
typedef void (*Uns__DaosAcl_Closure)
                 (const Uns__DaosAcl *message,
                  void *closure_data);
typedef void (*Uns__Entry_Closure)
                 (const Uns__Entry *message,
                  void *closure_data);
typedef void (*Uns__Properties_Closure)
                 (const Uns__Properties *message,
                  void *closure_data);
typedef void (*Uns__DunsAttribute_Closure)
                 (const Uns__DunsAttribute *message,
                  void *closure_data);

/* --- services --- */


/* --- descriptors --- */

extern const ProtobufCEnumDescriptor    uns__prop_type__descriptor;
extern const ProtobufCEnumDescriptor    uns__layout__descriptor;
extern const ProtobufCMessageDescriptor uns__daos_ace__descriptor;
extern const ProtobufCMessageDescriptor uns__daos_acl__descriptor;
extern const ProtobufCMessageDescriptor uns__entry__descriptor;
extern const ProtobufCMessageDescriptor uns__properties__descriptor;
extern const ProtobufCMessageDescriptor uns__duns_attribute__descriptor;

PROTOBUF_C__END_DECLS


#endif  /* PROTOBUF_C_DunsAttribute_2eproto__INCLUDED */
