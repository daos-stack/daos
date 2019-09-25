/*
 * (C) Copyright 2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * \file
 *
 * DAOS Unified Namespace defines for Lustre
 *
 * This include file is intended to replace missing Lustre includes and
 * to provide the necessary defines to allow for libduns build with
 * UNS for Lustre support
 */

#ifndef __DAOS_UNS_LUSTRE_H__
#define __DAOS_UNS_LUSTRE_H__

#include <linux/types.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* XXX re-define all required Lustre stuff here to allow for libduns compile
 * if no Luste distro/include installed at DAOS build time
 */

/* XXX should we also artificialy provide LUSTRE_VERSION_STRING, taken from
 * the Lustre sources version these defines have been extracted, here too ?
 * This will allow to check it against the version dynamicaly binded with,
 * by resolving llapi_get_version_string() method itself an run it !!...
 */

/* from /usr/include/linux/lustre/lustre_user.h */

#define LL_SUPER_MAGIC 0x0BD00BD0

#define LL_IOC_LMV_GETSTRIPE _IOWR('f', 241, struct lmv_user_md)

/**
 * File IDentifier.
 *
 * FID is a cluster-wide unique identifier of a file or an object (stripe).
 * FIDs are never reused.
 **/
struct lu_fid {
       /**
	* FID sequence. Sequence is a unit of migration: all files (objects)
	* with FIDs from a given sequence are stored on the same server.
	* Lustre should support 2^64 objects, so even if each sequence
	* has only a single object we can still enumerate 2^64 objects.
	**/
	__u64 f_seq;
	/* FID number within sequence. */
	__u32 f_oid;
	/**
	 * FID version, used to distinguish different versions (in the sense
	 * of snapshots, etc.) of the same file system object. Not currently
	 * used.
	 **/
	__u32 f_ver;
};

struct lmv_user_mds_data {
	struct lu_fid	lum_fid;
	__u32		lum_padding;
	__u32		lum_mds;
};

#define LOV_MAXPOOLNAME 15

/**
 * LOV/LMV foreign types
 **/
enum lustre_foreign_types {
	LU_FOREIGN_TYPE_NONE = 0,
	LU_FOREIGN_TYPE_DAOS = 0xda05,
	/* must be the max/last one */
	LU_FOREIGN_TYPE_UNKNOWN = 0xffffffff,
};

#define lmv_user_md lmv_user_md_v1
struct lmv_user_md_v1 {
	__u32	lum_magic;	 /* must be the first field */
	__u32	lum_stripe_count;  /* dirstripe count */
	__u32	lum_stripe_offset; /* MDT idx for default dirstripe */
	__u32	lum_hash_type;     /* Dir stripe policy */
	__u32	lum_type;	  /* LMV type: default */
	__u32	lum_padding1;
	__u32	lum_padding2;
	__u32	lum_padding3;
	char	lum_pool_name[LOV_MAXPOOLNAME + 1];
	struct	lmv_user_mds_data  lum_objects[0];
} __attribute__((packed));


/* from /usr/include/linux/lustre/lustre_idl.h */

/* foreign LMV EA */
struct lmv_foreign_md {
	__u32 lfm_magic;	/* magic number = LMV_MAGIC_FOREIGN */
	__u32 lfm_length;	/* length of lfm_value */
	__u32 lfm_type;		/* type, see LU_FOREIGN_TYPE_ */
	__u32 lfm_flags;	/* flags, type specific */
	char lfm_value[];	/* free format value */
};

#define LMV_MAGIC_V1	0x0CD20CD0    /* normal stripe lmv magic */

#define LMV_MAGIC_FOREIGN 0x0CD50CD0 /* magic for lmv foreign */

#if defined(__cplusplus)
}
#endif
#endif /* __DAOS_UNS_LUSTRE_H__ */
