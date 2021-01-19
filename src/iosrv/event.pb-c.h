/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: event.proto */

#ifndef PROTOBUF_C_event_2eproto__INCLUDED
#define PROTOBUF_C_event_2eproto__INCLUDED

#include <protobuf-c/protobuf-c.h>

PROTOBUF_C__BEGIN_DECLS

#if PROTOBUF_C_VERSION_NUMBER < 1003000
# error This file was generated by a newer version of protoc-c which is incompatible with your libprotobuf-c headers. Please update your headers.
#elif 1003003 < PROTOBUF_C_MIN_COMPILER_VERSION
# error This file was generated by an older version of protoc-c which is incompatible with your libprotobuf-c headers. Please regenerate this file with a newer version of protoc-c.
#endif


typedef struct _Shared__RASEvent Shared__RASEvent;
typedef struct _Shared__RASEvent__RankStateEventInfo Shared__RASEvent__RankStateEventInfo;
typedef struct _Shared__RASEvent__PoolSvcEventInfo Shared__RASEvent__PoolSvcEventInfo;
typedef struct _Shared__ClusterEventReq Shared__ClusterEventReq;
typedef struct _Shared__ClusterEventResp Shared__ClusterEventResp;


/* --- enums --- */


/* --- messages --- */

/*
 * RankStateEventInfo defines extended fields for rank state change events.
 */
struct  _Shared__RASEvent__RankStateEventInfo
{
  ProtobufCMessage base;
  /*
   * Control-plane harness instance index.
   */
  uint32_t instance;
  /*
   * Rank in error state.
   */
  protobuf_c_boolean errored;
  /*
   * Message associated with error.
   */
  char *error;
};
#define SHARED__RASEVENT__RANK_STATE_EVENT_INFO__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&shared__rasevent__rank_state_event_info__descriptor) \
    , 0, 0, (char *)protobuf_c_empty_string }


/*
 * PoolSvcEventInfo defines extended fields for pool service change events.
 */
struct  _Shared__RASEvent__PoolSvcEventInfo
{
  ProtobufCMessage base;
  /*
   * Pool service replica ranks.
   */
  size_t n_svc_reps;
  uint32_t *svc_reps;
  /*
   * Raft leadership term.
   */
  uint64_t version;
};
#define SHARED__RASEVENT__POOL_SVC_EVENT_INFO__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&shared__rasevent__pool_svc_event_info__descriptor) \
    , 0,NULL, 0 }


typedef enum {
  SHARED__RASEVENT__EXTENDED_INFO__NOT_SET = 0,
  SHARED__RASEVENT__EXTENDED_INFO_STR_INFO = 16,
  SHARED__RASEVENT__EXTENDED_INFO_RANK_STATE_INFO = 17,
  SHARED__RASEVENT__EXTENDED_INFO_POOL_SVC_INFO = 18
    PROTOBUF_C__FORCE_ENUM_TO_BE_INT_SIZE(SHARED__RASEVENT__EXTENDED_INFO)
} Shared__RASEvent__ExtendedInfoCase;

/*
 * RASEvent describes a RAS event in the DAOS system.
 */
struct  _Shared__RASEvent
{
  ProtobufCMessage base;
  /*
   * Unique event identifier, 64-char.
   */
  uint32_t id;
  /*
   * Human readable message describing event.
   */
  char *msg;
  /*
   * Fully qualified timestamp (us) incl timezone.
   */
  char *timestamp;
  /*
   * Event type.
   */
  uint32_t type;
  /*
   * Event severity.
   */
  uint32_t severity;
  /*
   * (optional) Hostname of node involved in event.
   */
  char *hostname;
  /*
   * (optional) DAOS rank involved in event.
   */
  uint32_t rank;
  /*
   * (optional) Hardware component involved in event.
   */
  char *hw_id;
  /*
   * (optional) Process involved in event.
   */
  char *proc_id;
  /*
   * (optional) Thread involved in event.
   */
  char *thread_id;
  /*
   * (optional) Job involved in event.
   */
  char *job_id;
  /*
   * (optional) Pool UUID involved in event.
   */
  char *pool_uuid;
  /*
   * (optional) Container UUID involved in event.
   */
  char *cont_uuid;
  /*
   * (optional) Object involved in event.
   */
  char *obj_id;
  /*
   * (optional) Recommended automatic action.
   */
  char *ctl_op;
  Shared__RASEvent__ExtendedInfoCase extended_info_case;
  union {
    /*
     * Opaque data blob.
     */
    char *str_info;
    Shared__RASEvent__RankStateEventInfo *rank_state_info;
    Shared__RASEvent__PoolSvcEventInfo *pool_svc_info;
  };
};
#define SHARED__RASEVENT__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&shared__rasevent__descriptor) \
    , 0, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, 0, 0, (char *)protobuf_c_empty_string, 0, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, (char *)protobuf_c_empty_string, SHARED__RASEVENT__EXTENDED_INFO__NOT_SET, {0} }


/*
 * ClusterEventReq communicates occurrence of a RAS event in the DAOS system.
 */
struct  _Shared__ClusterEventReq
{
  ProtobufCMessage base;
  /*
   * Sequence identifier for RAS events.
   */
  uint64_t sequence;
  /*
   * RAS event.
   */
  Shared__RASEvent *event;
};
#define SHARED__CLUSTER_EVENT_REQ__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&shared__cluster_event_req__descriptor) \
    , 0, NULL }


/*
 * RASEventResp acknowledges receipt of an event notification.
 */
struct  _Shared__ClusterEventResp
{
  ProtobufCMessage base;
  /*
   * Sequence identifier for RAS events.
   */
  uint64_t sequence;
  /*
   * DAOS error code.
   */
  int32_t status;
};
#define SHARED__CLUSTER_EVENT_RESP__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&shared__cluster_event_resp__descriptor) \
    , 0, 0 }


/* Shared__RASEvent__RankStateEventInfo methods */
void   shared__rasevent__rank_state_event_info__init
                     (Shared__RASEvent__RankStateEventInfo         *message);
/* Shared__RASEvent__PoolSvcEventInfo methods */
void   shared__rasevent__pool_svc_event_info__init
                     (Shared__RASEvent__PoolSvcEventInfo         *message);
/* Shared__RASEvent methods */
void   shared__rasevent__init
                     (Shared__RASEvent         *message);
size_t shared__rasevent__get_packed_size
                     (const Shared__RASEvent   *message);
size_t shared__rasevent__pack
                     (const Shared__RASEvent   *message,
                      uint8_t             *out);
size_t shared__rasevent__pack_to_buffer
                     (const Shared__RASEvent   *message,
                      ProtobufCBuffer     *buffer);
Shared__RASEvent *
       shared__rasevent__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   shared__rasevent__free_unpacked
                     (Shared__RASEvent *message,
                      ProtobufCAllocator *allocator);
/* Shared__ClusterEventReq methods */
void   shared__cluster_event_req__init
                     (Shared__ClusterEventReq         *message);
size_t shared__cluster_event_req__get_packed_size
                     (const Shared__ClusterEventReq   *message);
size_t shared__cluster_event_req__pack
                     (const Shared__ClusterEventReq   *message,
                      uint8_t             *out);
size_t shared__cluster_event_req__pack_to_buffer
                     (const Shared__ClusterEventReq   *message,
                      ProtobufCBuffer     *buffer);
Shared__ClusterEventReq *
       shared__cluster_event_req__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   shared__cluster_event_req__free_unpacked
                     (Shared__ClusterEventReq *message,
                      ProtobufCAllocator *allocator);
/* Shared__ClusterEventResp methods */
void   shared__cluster_event_resp__init
                     (Shared__ClusterEventResp         *message);
size_t shared__cluster_event_resp__get_packed_size
                     (const Shared__ClusterEventResp   *message);
size_t shared__cluster_event_resp__pack
                     (const Shared__ClusterEventResp   *message,
                      uint8_t             *out);
size_t shared__cluster_event_resp__pack_to_buffer
                     (const Shared__ClusterEventResp   *message,
                      ProtobufCBuffer     *buffer);
Shared__ClusterEventResp *
       shared__cluster_event_resp__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   shared__cluster_event_resp__free_unpacked
                     (Shared__ClusterEventResp *message,
                      ProtobufCAllocator *allocator);
/* --- per-message closures --- */

typedef void (*Shared__RASEvent__RankStateEventInfo_Closure)
                 (const Shared__RASEvent__RankStateEventInfo *message,
                  void *closure_data);
typedef void (*Shared__RASEvent__PoolSvcEventInfo_Closure)
                 (const Shared__RASEvent__PoolSvcEventInfo *message,
                  void *closure_data);
typedef void (*Shared__RASEvent_Closure)
                 (const Shared__RASEvent *message,
                  void *closure_data);
typedef void (*Shared__ClusterEventReq_Closure)
                 (const Shared__ClusterEventReq *message,
                  void *closure_data);
typedef void (*Shared__ClusterEventResp_Closure)
                 (const Shared__ClusterEventResp *message,
                  void *closure_data);

/* --- services --- */


/* --- descriptors --- */

extern const ProtobufCMessageDescriptor shared__rasevent__descriptor;
extern const ProtobufCMessageDescriptor shared__rasevent__rank_state_event_info__descriptor;
extern const ProtobufCMessageDescriptor shared__rasevent__pool_svc_event_info__descriptor;
extern const ProtobufCMessageDescriptor shared__cluster_event_req__descriptor;
extern const ProtobufCMessageDescriptor shared__cluster_event_resp__descriptor;

PROTOBUF_C__END_DECLS


#endif  /* PROTOBUF_C_event_2eproto__INCLUDED */
