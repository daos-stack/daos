/**
 * (C) Copyright 2017-2019 Intel Corporation.
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
#ifndef __DFUSE_BULK_H__
#define __DFUSE_BULK_H__

#include <cart/api.h>

struct dfuse_local_bulk {
	void		*buf;
	crt_bulk_t	 handle;
	size_t		 len;
};

bool dfuse_bulk_alloc(crt_context_t ctx, void *ptr, off_t bulk_offset,
		      size_t len, bool read_only);
void dfuse_bulk_free(void *ptr, off_t bulk_offset);

#define DFUSE_BULK_ALLOC(ctx, ptr, field, len, read_only)		\
	dfuse_bulk_alloc((ctx), (ptr), offsetof(__typeof__(*ptr), field), \
		       (len), (read_only))
#define DFUSE_BULK_FREE(ptr, field)	\
	dfuse_bulk_free((ptr), offsetof(__typeof__(*ptr), field))

#endif /* __DFUSE_BULK_H__ */
