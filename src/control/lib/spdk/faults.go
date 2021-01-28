//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package spdk

import (
	"fmt"

	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
)

var (
	FaultUnknown = spdkFault(
		code.SpdkUnknown,
		"unknown SPDK bindings error",
		"",
	)
	FaultCtrlrNoHealth = spdkFault(
		code.SpdkCtrlrNoHealth,
		"NVMe controller details are missing health statistics",
		"",
	)
	FaultBindingRetNull = spdkFault(
		code.SpdkBindingRetNull,
		"SPDK binding unexpectedly returned NULL",
		"",
	)
)

func FaultBindingFailed(rc int, errMsg string) *fault.Fault {
	return spdkFault(
		code.SpdkBindingFailed,
		fmt.Sprintf("SPDK binding failed, rc: %d, %s", rc, errMsg),
		"",
	)
}

func spdkFault(code code.Code, desc, res string) *fault.Fault {
	return &fault.Fault{
		Domain:      "spdk",
		Code:        code,
		Description: desc,
		Resolution:  res,
	}
}
