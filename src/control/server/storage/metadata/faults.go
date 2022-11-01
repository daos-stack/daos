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
func FaultBadFilesystem(fs string) *fault.Fault {
	return metadataFault(
		code.ControlMetadataBadFilesystem,
		fmt.Sprintf("%q is not a usable filesystem for the control metadata storage directory", fs),
		"Configure the control_metadata path to a directory that is not on a networked filesystem",
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
