package daos

import (
	"github.com/google/uuid"
	"github.com/pkg/errors"
)

/*
#include <daos/common.h>
#include <daos_prop.h>
#include <daos_cont.h>
*/
import "C"

type (
	ContainerLayout   uint16
	ContainerOpenFlag uint

	POSIXAttributes struct {
		ChunkSize       uint64      `json:"chunk_size,omitempty"`
		ObjectClass     ObjectClass `json:"object_class,omitempty"`
		DirObjectClass  ObjectClass `json:"dir_object_class,omitempty"`
		FileObjectClass ObjectClass `json:"file_object_class,omitempty"`
		ConsistencyMode uint32      `json:"cons_mode,omitempty"`
		Hints           string      `json:"hints,omitempty"`
	}

	ContainerInfo struct {
		PoolUUID         uuid.UUID        `json:"pool_uuid"`
		UUID             uuid.UUID        `json:"container_uuid"`
		Label            string           `json:"container_label,omitempty"`
		LatestSnapshot   uint64           `json:"latest_snapshot"`
		RedundancyFactor uint32           `json:"redundancy_factor"`
		NumHandles       uint32           `json:"num_handles"`
		NumSnapshots     uint32           `json:"num_snapshots"`
		OpenTime         uint64           `json:"open_time"`
		CloseModifyTime  uint64           `json:"close_modify_time"`
		Type             ContainerLayout  `json:"container_type"`
		POSIXAttributes  *POSIXAttributes `json:"posix_attributes,omitempty"`
	}
)

const (
	ContainerLayoutUnknown  ContainerLayout = C.DAOS_PROP_CO_LAYOUT_UNKNOWN
	ContainerLayoutPOSIX    ContainerLayout = C.DAOS_PROP_CO_LAYOUT_POSIX
	ContainerLayoutHDF5     ContainerLayout = C.DAOS_PROP_CO_LAYOUT_HDF5
	ContainerLayoutPython   ContainerLayout = C.DAOS_PROP_CO_LAYOUT_PYTHON
	ContainerLayoutSpark    ContainerLayout = C.DAOS_PROP_CO_LAYOUT_SPARK
	ContainerLayoutDatabase ContainerLayout = C.DAOS_PROP_CO_LAYOUT_DATABASE
	ContainerLayoutRoot     ContainerLayout = C.DAOS_PROP_CO_LAYOUT_ROOT
	ContainerLayoutSeismic  ContainerLayout = C.DAOS_PROP_CO_LAYOUT_SEISMIC
	ContainerLayoutMeteo    ContainerLayout = C.DAOS_PROP_CO_LAYOUT_METEO

	ContainerOpenFlagReadOnly  ContainerOpenFlag = C.DAOS_COO_RO
	ContainerOpenFlagReadWrite ContainerOpenFlag = C.DAOS_COO_RW
	ContainerOpenFlagExclusive ContainerOpenFlag = C.DAOS_COO_EX
	ContainerOpenFlagForce     ContainerOpenFlag = C.DAOS_COO_FORCE
	ContainerOpenFlagMdStats   ContainerOpenFlag = C.DAOS_COO_RO_MDSTATS
	ContainerOpenFlagEvict     ContainerOpenFlag = C.DAOS_COO_EVICT
	ContainerOpenFlagEvictAll  ContainerOpenFlag = C.DAOS_COO_EVICT_ALL
)

func (l *ContainerLayout) FromString(in string) error {
	cStr := C.CString(in)
	defer freeString(cStr)
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
