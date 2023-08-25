package daos

import (
	"strings"

	"github.com/pkg/errors"
)

/*
#include <fcntl.h>

#include <daos.h>
#include <daos_fs.h>
*/
import "C"

type (
	DFSConsistencyMode uint32
)

var (
	DFSConsistencyModeRelaxed   DFSConsistencyMode = C.DFS_RELAXED
	DFSConsistencyModeBalanced  DFSConsistencyMode = C.DFS_BALANCED
	DFSConsistencyModeReadOnly  DFSConsistencyMode = C.O_RDONLY
	DFSConsistencyModeReadWrite DFSConsistencyMode = C.O_RDWR
)

func (m DFSConsistencyMode) String() string {
	switch m {
	case DFSConsistencyModeRelaxed:
		return "relaxed"
	case DFSConsistencyModeBalanced:
		return "balanced"
	case DFSConsistencyModeReadOnly:
		return "readonly"
	case DFSConsistencyModeReadWrite:
		return "readwrite"
	default:
		return "unknown"
	}
}

func (m *DFSConsistencyMode) FromString(in string) error {
	switch strings.ToLower(in) {
	case "relaxed":
		*m = DFSConsistencyModeRelaxed
	case "balanced":
		*m = DFSConsistencyModeBalanced
	case "readonly":
		*m = DFSConsistencyModeReadOnly
	case "readwrite":
		*m = DFSConsistencyModeReadWrite
	default:
		return errors.Errorf("unknown DFS consistency mode %q", in)
	}

	return nil
}
