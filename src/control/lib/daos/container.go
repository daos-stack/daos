//
// (C) Copyright 2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"encoding/json"
	"sort"
	"strings"

	"github.com/google/uuid"
	"github.com/pkg/errors"
)

/*
#include <stdint.h>

#include <daos_cont.h>
#include <daos/common.h>

#cgo LDFLAGS: -ldaos_common
*/
import "C"

type (
	// ContainerLayout represents the layout of a container.
	ContainerLayout uint16
	// ContainerQueryOption is used to supply container open options.
	ContainerOpenFlag uint
)

const (
	// ContainerOpenFlagReadOnly opens the container in read-only mode.
	ContainerOpenFlagReadOnly ContainerOpenFlag = C.DAOS_COO_RO
	// ContainerOpenFlagReadWrite opens the container in read-write mode.
	ContainerOpenFlagReadWrite ContainerOpenFlag = C.DAOS_COO_RW
	// ContainerOpenFlagExclusive opens the container in exclusive read-write mode.
	ContainerOpenFlagExclusive ContainerOpenFlag = C.DAOS_COO_EX
	// ContainerOpenFlagForce skips container health checks.
	ContainerOpenFlagForce ContainerOpenFlag = C.DAOS_COO_FORCE
	// ContainerOpenFlagReadOnlyMetadata skips container metadata updates.
	ContainerOpenFlagReadOnlyMetadata ContainerOpenFlag = C.DAOS_COO_RO_MDSTATS
	// ContainerOpenFlagEvict evicts the current user's open handles.
	ContainerOpenFlagEvict ContainerOpenFlag = C.DAOS_COO_EVICT
	// ContainerOpenFlagEvictAll evicts all open handles.
	ContainerOpenFlagEvictAll ContainerOpenFlag = C.DAOS_COO_EVICT_ALL

	// ContainerLayoutUnknown represents an unknown container layout.
	ContainerLayoutUnknown ContainerLayout = C.DAOS_PROP_CO_LAYOUT_UNKNOWN
	// ContainerLayoutPOSIX represents a POSIX container layout.
	ContainerLayoutPOSIX ContainerLayout = C.DAOS_PROP_CO_LAYOUT_POSIX
	// ContainerLayoutHDF5 represents an HDF5 container layout.
	ContainerLayoutHDF5 ContainerLayout = C.DAOS_PROP_CO_LAYOUT_HDF5
	// ContainerLayoutPython represents a Python container layout.
	ContainerLayoutPython ContainerLayout = C.DAOS_PROP_CO_LAYOUT_PYTHON
	// ContainerLayoutSpark represents a Spark container layout.
	ContainerLayoutSpark ContainerLayout = C.DAOS_PROP_CO_LAYOUT_SPARK
	// ContainerLayoutDatabase represents a database container layout.
	ContainerLayoutDatabase ContainerLayout = C.DAOS_PROP_CO_LAYOUT_DATABASE
	// ContainerLayoutRoot represents a root container layout.
	ContainerLayoutRoot ContainerLayout = C.DAOS_PROP_CO_LAYOUT_ROOT
	// ContainerLayoutSeismic represents a seismic container layout.
	ContainerLayoutSeismic ContainerLayout = C.DAOS_PROP_CO_LAYOUT_SEISMIC
	// ContainerLayoutMeteo represents a meteo container layout.
	ContainerLayoutMeteo ContainerLayout = C.DAOS_PROP_CO_LAYOUT_METEO
)

func (cof ContainerOpenFlag) String() string {
	flagStrs := []string{}
	if cof&ContainerOpenFlagReadOnly != 0 {
		flagStrs = append(flagStrs, "read-only")
	}
	if cof&ContainerOpenFlagReadWrite != 0 {
		flagStrs = append(flagStrs, "read-write")
	}
	if cof&ContainerOpenFlagExclusive != 0 {
		flagStrs = append(flagStrs, "exclusive")
	}
	if cof&ContainerOpenFlagForce != 0 {
		flagStrs = append(flagStrs, "force")
	}
	if cof&ContainerOpenFlagReadOnlyMetadata != 0 {
		flagStrs = append(flagStrs, "read-only-metadata")
	}
	if cof&ContainerOpenFlagEvict != 0 {
		flagStrs = append(flagStrs, "evict")
	}
	if cof&ContainerOpenFlagEvictAll != 0 {
		flagStrs = append(flagStrs, "evict-all")
	}
	sort.Strings(flagStrs)
	return strings.Join(flagStrs, ",")
}

// FromString converts a string to a ContainerLayout.
func (l *ContainerLayout) FromString(in string) error {
	cStr, free := toCString(in)
	defer free()
	C.daos_parse_ctype(cStr, (*C.uint16_t)(l))

	if *l == ContainerLayoutUnknown {
		return errors.Errorf("unknown container layout %q", in)
	}

	return nil
}

func (l ContainerLayout) String() string {
	var cType [10]C.char
	C.daos_unparse_ctype(C.ushort(l), &cType[0])
	return C.GoString(&cType[0])
}

func (l ContainerLayout) MarshalJSON() ([]byte, error) {
	return []byte(`"` + l.String() + `"`), nil
}

func (l *ContainerLayout) UnmarshalJSON(data []byte) error {
	return l.FromString(string(data[1 : len(data)-1]))
}

type (
	// POSIXAttributes contains extended information about POSIX-layout containers.
	POSIXAttributes struct {
		ChunkSize       uint64      `json:"chunk_size,omitempty"`
		ObjectClass     ObjectClass `json:"object_class,omitempty"`
		DirObjectClass  ObjectClass `json:"dir_object_class,omitempty"`
		FileObjectClass ObjectClass `json:"file_object_class,omitempty"`
		ConsistencyMode uint32      `json:"cons_mode,omitempty"`
		Hints           string      `json:"hints,omitempty"`
	}

	// ContainerInfo contains information about the Container.
	ContainerInfo struct {
		PoolUUID         uuid.UUID       `json:"pool_uuid"`
		ContainerUUID    uuid.UUID       `json:"container_uuid"`
		ContainerLabel   string          `json:"container_label,omitempty"`
		LatestSnapshot   HLC             `json:"latest_snapshot"`
		RedundancyFactor uint32          `json:"redundancy_factor"`
		NumHandles       uint32          `json:"num_handles"`
		NumSnapshots     uint32          `json:"num_snapshots"`
		OpenTime         HLC             `json:"open_time"`
		CloseModifyTime  HLC             `json:"close_modify_time"`
		Type             ContainerLayout `json:"container_type"`
		Health           string          `json:"health"`
		*POSIXAttributes `json:",omitempty"`
	}
)

// Name returns an identifier for the container (Label, if set, falling back to UUID).
func (ci *ContainerInfo) Name() string {
	if ci.ContainerLabel == "" {
		return ci.ContainerUUID.String()
	}
	return ci.ContainerLabel
}

func (ci *ContainerInfo) String() string {
	return ci.Name()
}

func (ci *ContainerInfo) MarshalJSON() ([]byte, error) {
	checkZeroHLC := func(hlc HLC) string {
		if hlc.IsZero() {
			return ""
		}
		return hlc.String()
	}

	type toJSON ContainerInfo
	return json.Marshal(&struct {
		toJSON
		LatestSnapshot  string `json:"latest_snapshot,omitempty"`
		OpenTime        string `json:"open_time,omitempty"`
		CloseModifyTime string `json:"close_modify_time,omitempty"`
	}{
		toJSON:          toJSON(*ci),
		LatestSnapshot:  checkZeroHLC(ci.LatestSnapshot),
		OpenTime:        checkZeroHLC(ci.OpenTime),
		CloseModifyTime: checkZeroHLC(ci.CloseModifyTime),
	})
}
