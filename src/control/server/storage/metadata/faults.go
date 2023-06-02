//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package metadata

import (
	"fmt"

	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
	"github.com/daos-stack/daos/src/control/provider/system"
)

var (
	FaultUnknown = metadataFault(
		code.ControlMetadataUnknown,
		"unknown control metadata error",
		"",
	)
)

// FaultBadFilesystem is an error that can occur if the control metadata path points at a
// filesystem that cannot be used.
func FaultBadFilesystem(fs *system.FsType) *fault.Fault {
	noSUIDStr := ""
	if fs.NoSUID {
		noSUIDStr = " with nosuid set"
	}

	return metadataFault(
		code.ControlMetadataBadFilesystem,
		fmt.Sprintf("%q%s is not usable for the control metadata storage directory", fs.Name, noSUIDStr),
		"Configure the control_metadata path to a directory that is not on a networked or nosuid filesystem",
	)
}

func metadataFault(code code.Code, desc, res string) *fault.Fault {
	return &fault.Fault{
		Domain:      "controlmetadata",
		Code:        code,
		Description: desc,
		Resolution:  res,
	}
}
