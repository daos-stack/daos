
/*
 * Copyright(c) 2012-2021 Intel Corporation
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "ocf/ocf_io.h"
#include "ocf/ocf_volume.h"

const char *ocf_uuid_to_str_wrapper(const struct ocf_volume_uuid *uuid) {
	return ocf_uuid_to_str(uuid);
}
