//
// (C) Copyright 2020-2022 Intel Corporation.
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
)

func FaultBindingRetNull(msg string) *fault.Fault {
	return spdkFault(
		code.SpdkBindingRetNull,
		fmt.Sprintf("SPDK binding unexpectedly returned NULL, %s", msg),
		"",
	)
}

func FaultBindingFailed(rc int, msg string) *fault.Fault {
	return spdkFault(
		code.SpdkBindingFailed,
		fmt.Sprintf("SPDK binding failed, rc: %d, %s", rc, msg),
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
