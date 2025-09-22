//
// (C) Copyright 2022 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

/*
#include <daos_types.h>
#include <daos/metrics.h>
*/
import "C"

import "time"

const (
	// DefaultCartTimeout defines the default timeout for cart operations.
	DefaultCartTimeout = 60 * time.Second // Should use CRT_DEFAULT_TIMEOUT_S but it's not exported

	// MaxAttributeNameLength defines the maximum length of an attribute name.
	MaxAttributeNameLength = C.DAOS_ATTR_NAME_MAX

	// ClientMetricsDumpPoolAttr defines the attribute name for the pool hosting the telemetry dumps.
	ClientMetricsDumpPoolAttr = C.DAOS_CLIENT_METRICS_DUMP_POOL_ATTR

	// ClientMetricsDumpContAttr defines the attribute name for the container hosting the telemetry dumps.
	ClientMetricsDumpContAttr = C.DAOS_CLIENT_METRICS_DUMP_CONT_ATTR

	// ClientMetricsDumpDirAttr defines the attribute name for the directory hosting the telemetry dumps.
	ClientMetricsDumpDirAttr = C.DAOS_CLIENT_METRICS_DUMP_DIR_ATTR
)
