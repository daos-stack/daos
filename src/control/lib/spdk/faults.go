//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
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
