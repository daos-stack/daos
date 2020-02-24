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
