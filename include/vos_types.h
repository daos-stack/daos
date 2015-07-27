/**
 * Copyright (c) 2015, Intel Corporation.
*/


#ifndef  __VOS_TYPES_H
#define  __VOS_TYPES_H


typedef struct {
          vs_size_t    ba_objects; /* Number of ba objects in this epoch*/
          vs_size_t    kv_objects;  /* Number of kv objects in this epoch*/
          vs_epoch_t   highest_epoch; /* Highest epoch in this pool. Not HCE*/
          vs_epoch_t   lowest_epoch; /* Highest epoch in this pool. Not HCE*/
          vs_size_t    maxbytes; /* Total space available */
          vs_size_t    savail; /* Current available space (upated on obj commit?) */
}vs_stat_t;

struct kv_iter;
typedef struct kv_iter* kv_iterator_t;


#endif
