/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: acl.proto */

#ifndef PROTOBUF_C_acl_2eproto__INCLUDED
#define PROTOBUF_C_acl_2eproto__INCLUDED

#include <protobuf-c/protobuf-c.h>

PROTOBUF_C__BEGIN_DECLS

#if PROTOBUF_C_VERSION_NUMBER < 1003000
# error This file was generated by a newer version of protoc-c which is incompatible with your libprotobuf-c headers. Please update your headers.
#elif 1003001 < PROTOBUF_C_MIN_COMPILER_VERSION
# error This file was generated by an older version of protoc-c which is incompatible with your libprotobuf-c headers. Please regenerate this file with a newer version of protoc-c.
#endif


typedef struct _Mgmt__ACLResp Mgmt__ACLResp;
typedef struct _Mgmt__GetACLReq Mgmt__GetACLReq;
typedef struct _Mgmt__ModifyACLReq Mgmt__ModifyACLReq;
typedef struct _Mgmt__DeleteACLReq Mgmt__DeleteACLReq;


/* --- enums --- */


/* --- messages --- */

/*
 * Response to ACL-related requests includes the command status and current ACL
 */
struct  _Mgmt__ACLResp
{
  ProtobufCMessage base;
  /*
   * DAOS error code
   */
  int32_t status;
  /*
   * List of ACEs in short string format
   */
  size_t n_acl;
  char **acl;
};
#define MGMT__ACLRESP__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__aclresp__descriptor) \
    , 0, 0,NULL }


/*
 * Request to fetch an ACL
 */
struct  _Mgmt__GetACLReq
{
  ProtobufCMessage base;
  /*
   * Target UUID
   */
  char *uuid;
};
#define MGMT__GET_ACLREQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__get_aclreq__descriptor) \
    , (char *)protobuf_c_empty_string }


/*
 * Request to modify an ACL
 * Results depend on the specific modification command.
 */
struct  _Mgmt__ModifyACLReq
{
  ProtobufCMessage base;
  /*
   * Target UUID
   */
  char *uuid;
  /*
   * List of ACEs to overwrite ACL with
   */
  size_t n_acl;
  char **acl;
};
#define MGMT__MODIFY_ACLREQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__modify_aclreq__descriptor) \
    , (char *)protobuf_c_empty_string, 0,NULL }


/*
 * Delete a principal's entry from the ACL
 */
struct  _Mgmt__DeleteACLReq
{
  ProtobufCMessage base;
  /*
   * Target UUID
   */
  char *uuid;
  /*
   * Principal whose entry is to be deleted
   */
  char *principal;
};
#define MGMT__DELETE_ACLREQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&mgmt__delete_aclreq__descriptor) \
    , (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string }


/* Mgmt__ACLResp methods */
void   mgmt__aclresp__init
                     (Mgmt__ACLResp         *message);
size_t mgmt__aclresp__get_packed_size
                     (const Mgmt__ACLResp   *message);
size_t mgmt__aclresp__pack
                     (const Mgmt__ACLResp   *message,
                      uint8_t             *out);
size_t mgmt__aclresp__pack_to_buffer
                     (const Mgmt__ACLResp   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__ACLResp *
       mgmt__aclresp__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__aclresp__free_unpacked
                     (Mgmt__ACLResp *message,
                      ProtobufCAllocator *allocator);
/* Mgmt__GetACLReq methods */
void   mgmt__get_aclreq__init
                     (Mgmt__GetACLReq         *message);
size_t mgmt__get_aclreq__get_packed_size
                     (const Mgmt__GetACLReq   *message);
size_t mgmt__get_aclreq__pack
                     (const Mgmt__GetACLReq   *message,
                      uint8_t             *out);
size_t mgmt__get_aclreq__pack_to_buffer
                     (const Mgmt__GetACLReq   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__GetACLReq *
       mgmt__get_aclreq__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__get_aclreq__free_unpacked
                     (Mgmt__GetACLReq *message,
                      ProtobufCAllocator *allocator);
/* Mgmt__ModifyACLReq methods */
void   mgmt__modify_aclreq__init
                     (Mgmt__ModifyACLReq         *message);
size_t mgmt__modify_aclreq__get_packed_size
                     (const Mgmt__ModifyACLReq   *message);
size_t mgmt__modify_aclreq__pack
                     (const Mgmt__ModifyACLReq   *message,
                      uint8_t             *out);
size_t mgmt__modify_aclreq__pack_to_buffer
                     (const Mgmt__ModifyACLReq   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__ModifyACLReq *
       mgmt__modify_aclreq__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__modify_aclreq__free_unpacked
                     (Mgmt__ModifyACLReq *message,
                      ProtobufCAllocator *allocator);
/* Mgmt__DeleteACLReq methods */
void   mgmt__delete_aclreq__init
                     (Mgmt__DeleteACLReq         *message);
size_t mgmt__delete_aclreq__get_packed_size
                     (const Mgmt__DeleteACLReq   *message);
size_t mgmt__delete_aclreq__pack
                     (const Mgmt__DeleteACLReq   *message,
                      uint8_t             *out);
size_t mgmt__delete_aclreq__pack_to_buffer
                     (const Mgmt__DeleteACLReq   *message,
                      ProtobufCBuffer     *buffer);
Mgmt__DeleteACLReq *
       mgmt__delete_aclreq__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   mgmt__delete_aclreq__free_unpacked
                     (Mgmt__DeleteACLReq *message,
                      ProtobufCAllocator *allocator);
/* --- per-message closures --- */

typedef void (*Mgmt__ACLResp_Closure)
                 (const Mgmt__ACLResp *message,
                  void *closure_data);
typedef void (*Mgmt__GetACLReq_Closure)
                 (const Mgmt__GetACLReq *message,
                  void *closure_data);
typedef void (*Mgmt__ModifyACLReq_Closure)
                 (const Mgmt__ModifyACLReq *message,
                  void *closure_data);
typedef void (*Mgmt__DeleteACLReq_Closure)
                 (const Mgmt__DeleteACLReq *message,
                  void *closure_data);

/* --- services --- */


/* --- descriptors --- */

extern const ProtobufCMessageDescriptor mgmt__aclresp__descriptor;
extern const ProtobufCMessageDescriptor mgmt__get_aclreq__descriptor;
extern const ProtobufCMessageDescriptor mgmt__modify_aclreq__descriptor;
extern const ProtobufCMessageDescriptor mgmt__delete_aclreq__descriptor;

PROTOBUF_C__END_DECLS


#endif  /* PROTOBUF_C_acl_2eproto__INCLUDED */
