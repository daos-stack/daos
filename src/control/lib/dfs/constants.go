package dfs

import (
	"strings"

	"github.com/pkg/errors"
)

/*
#include <fcntl.h>

#include <daos.h>
#include <daos_fs.h>
#include <daos_fs_sys.h>
*/
import "C"

type (
	ConsistencyMode uint32
)

var (
	ConsistencyModeRelaxed   ConsistencyMode = C.DFS_RELAXED
	ConsistencyModeBalanced  ConsistencyMode = C.DFS_BALANCED
	ConsistencyModeReadOnly  ConsistencyMode = C.O_RDONLY
	ConsistencyModeReadWrite ConsistencyMode = C.O_RDWR
)

func (m ConsistencyMode) String() string {
	switch m {
	case ConsistencyModeRelaxed:
		return "relaxed"
	case ConsistencyModeBalanced:
		return "balanced"
	case ConsistencyModeReadOnly:
		return "readonly"
	case ConsistencyModeReadWrite:
		return "readwrite"
	default:
		return "unknown"
	}
}

func (m *ConsistencyMode) FromString(in string) error {
	switch strings.ToLower(in) {
	case "relaxed":
		*m = ConsistencyModeRelaxed
	case "balanced":
		*m = ConsistencyModeBalanced
	case "readonly":
		*m = ConsistencyModeReadOnly
	case "readwrite":
		*m = ConsistencyModeReadWrite
	default:
		return errors.Errorf("unknown DFS consistency mode %q", in)
	}

	return nil
}

type SysFlag C.int

const (
	SysFlagNoCache SysFlag = C.DFS_SYS_NO_CACHE
	SysFlagNoLock  SysFlag = C.DFS_SYS_NO_LOCK
)
