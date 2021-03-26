//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package pbin

import (
	"fmt"

	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
)

func PrivilegedHelperNotAvailable(helperName string) *fault.Fault {
	return pbinFault(
		code.PrivilegedHelperNotAvailable,
		fmt.Sprintf("the privileged helper (%s) was not found or is not executable", helperName),
		"check the DAOS admin guide for details on privileged helper setup",
	)
}

func PrivilegedHelperNotPrivileged(helperName string) *fault.Fault {
	return pbinFault(
		code.PrivilegedHelperNotPrivileged,
		fmt.Sprintf("the privileged helper (%s) does not have root permissions", helperName),
		"check the DAOS admin guide for details on privileged helper setup",
	)
}

func PrivilegedHelperRequestFailed(message string) *fault.Fault {
	return pbinFault(
		code.PrivilegedHelperRequestFailed,
		message, "",
	)
}

func pbinFault(code code.Code, message, resolution string) *fault.Fault {
	return &fault.Fault{
		Domain:      "pbin",
		Code:        code,
		Description: message,
		Resolution:  resolution,
	}
}
