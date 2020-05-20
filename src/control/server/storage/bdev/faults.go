//
// (C) Copyright 2019-2020 Intel Corporation.
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

package bdev

import (
	"fmt"

	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
)

var (
	FaultUnknown = bdevFault(code.BdevUnknown, "unknown bdev error", "")
)

func FaultPCIAddrNotFound(pciAddr string) *fault.Fault {
	return bdevFault(
		code.BdevPCIAddressNotFound,
		fmt.Sprintf("request contains NVMe PCI address %q that can't be found", pciAddr),
		"check your configuration is correct and devices with given PCI addresses exist on server host",
	)
}

func FaultBadPCIAddr(pciAddr string) *fault.Fault {
	return bdevFault(
		code.BdevBadPCIAddress,
		fmt.Sprintf("request contains invalid NVMe PCI address %q", pciAddr),
		"check your configuration, restart the server, and retry the operation",
	)
}

func FaultFormatUnknownClass(class string) *fault.Fault {
	return bdevFault(
		code.BdevFormatUnknownClass,
		fmt.Sprintf("format request contains unhandled block device class %q", class),
		"check your configuration and restart the server",
	)
}

func FaultFormatError(err error) *fault.Fault {
	return bdevFault(
		code.BdevFormatFailure,
		fmt.Sprintf("NVMe format failed: %s", err), "",
	)
}

func bdevFault(code code.Code, desc, res string) *fault.Fault {
	return &fault.Fault{
		Domain:      "bdev",
		Code:        code,
		Description: desc,
		Resolution:  res,
	}
}
