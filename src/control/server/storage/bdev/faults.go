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
	// FaultUnknown represents an unspecified bdev error.
	FaultUnknown = bdevFault(code.BdevUnknown, "unknown bdev error", "")

	// FaultDuplicateDevices represents an error where a user provided duplicate
	// device IDs in an input.
	FaultDuplicateDevices = bdevFault(code.BdevDuplicatesInDeviceList,
		"duplicates in NVMe device list",
		"check your device list and try again")

	// FaultNoFilterMatch represents an error where no devices were found that
	// matched user-provided filter criteria.
	FaultNoFilterMatch = bdevFault(code.BdevNoDevicesMatchFilter,
		"no NVMe device controllers matched the filter criteria",
		"adjust or relax the filters and try again")
)

// FaultPCIAddrNotFound creates a Fault for the case where no NVMe storage devices
// match a given PCI address.
func FaultPCIAddrNotFound(pciAddr string) *fault.Fault {
	return bdevFault(
		code.BdevPCIAddressNotFound,
		fmt.Sprintf("request contains NVMe PCI address %q that can't be found", pciAddr),
		"check your configuration is correct and devices with given PCI addresses exist on server host",
	)
}

// FaultBadPCIAddr creates a Fault for the case where a user-provided PCI address
// was invalid.
func FaultBadPCIAddr(pciAddr string) *fault.Fault {
	return bdevFault(
		code.BdevBadPCIAddress,
		fmt.Sprintf("request contains invalid NVMe PCI address %q", pciAddr),
		"check your configuration, restart the server, and retry the operation",
	)
}

// FaultFormatUnknownClass creates a Fault for the case where a user requested a
// device format for an unknown device class.
func FaultFormatUnknownClass(class string) *fault.Fault {
	return bdevFault(
		code.BdevFormatUnknownClass,
		fmt.Sprintf("format request contains unhandled block device class %q", class),
		"check your configuration and restart the server",
	)
}

// FaultFormatError creates a Fault for the case where an attempted device format
// failed.
func FaultFormatError(pciAddress string, err error) *fault.Fault {
	return bdevFault(
		code.BdevFormatFailure,
		fmt.Sprintf("NVMe format failed on %q: %s", pciAddress, err),
		"",
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
