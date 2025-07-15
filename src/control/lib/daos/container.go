//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import "github.com/google/uuid"

/*
#include <stdint.h>

#include <daos_cont.h>
*/
import "C"

const (
	// ContainerOpenFlagReadOnly opens the container in read-only mode.
	ContainerOpenFlagReadOnly = C.DAOS_COO_RO
	// ContainerOpenFlagReadWrite opens the container in read-write mode.
	ContainerOpenFlagReadWrite = C.DAOS_COO_RW
	// ContainerOpenFlagExclusive opens the container in exclusive read-write mode.
	ContainerOpenFlagExclusive = C.DAOS_COO_EX
	// ContainerOpenFlagForce skips container health checks.
	ContainerOpenFlagForce = C.DAOS_COO_FORCE
	// ContainerOpenFlagReadOnlyMetadata skips container metadata updates.
	ContainerOpenFlagReadOnlyMetadata = C.DAOS_COO_RO_MDSTATS
	// ContainerOpenFlagEvict evicts the current user's open handles.
	ContainerOpenFlagEvict = C.DAOS_COO_EVICT
	// ContainerOpenFlagEvictAll evicts all open handles.
	ContainerOpenFlagEvictAll = C.DAOS_COO_EVICT_ALL
)

// ContainerInfo contains information about the Container.
type ContainerInfo struct {
	PoolUUID         uuid.UUID `json:"pool_uuid"`
	ContainerUUID    uuid.UUID `json:"container_uuid"`
	ContainerLabel   string    `json:"container_label,omitempty"`
	LatestSnapshot   uint64    `json:"latest_snapshot"`
	RedundancyFactor uint32    `json:"redundancy_factor"`
	NumHandles       uint32    `json:"num_handles"`
	NumSnapshots     uint32    `json:"num_snapshots"`
	OpenTime         uint64    `json:"open_time"`
	CloseModifyTime  uint64    `json:"close_modify_time"`
	Type             string    `json:"container_type"`
	ObjectClass      string    `json:"object_class,omitempty"`
	DirObjectClass   string    `json:"dir_object_class,omitempty"`
	FileObjectClass  string    `json:"file_object_class,omitempty"`
	CHints           string    `json:"hints,omitempty"`
	ChunkSize        uint64    `json:"chunk_size,omitempty"`
	Health           string    `json:"health,omitempty"` // FIXME (DAOS-10028): Should be derived from props
}

// Name returns an identifier for the container (Label, if set, falling back to UUID).
func (ci *ContainerInfo) Name() string {
	if ci.ContainerLabel == "" {
		return ci.ContainerUUID.String()
	}
	return ci.ContainerLabel
}
