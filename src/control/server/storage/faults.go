//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"fmt"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
)

// FaultBdevHugepagesDisabled is a Fault for when bdev backend is called but
// hugepages have been disabled in config.
var FaultBdevHugepagesDisabled = storageFault(
	code.BdevHugepagesDisabled,
	"bdev storage cannot be accessed as hugepages have been disabled in config",
	"remove nr_hugepages parameter from config to have the value automatically calculated",
)

// FaultBdevNotFound creates a Fault for the case where no NVMe storage devices
// match expected PCI addresses.
func FaultBdevNotFound(bdevs ...string) *fault.Fault {
	return storageFault(
		code.BdevNotFound,
		fmt.Sprintf("NVMe SSD%s %v not found", common.Pluralise("", len(bdevs)), bdevs),
		fmt.Sprintf("check SSD%s %v that are specified in server config exist", common.Pluralise("", len(bdevs)), bdevs),
	)
}

func storageFault(code code.Code, desc, res string) *fault.Fault {
	return &fault.Fault{
		Domain:      "storage",
		Code:        code,
		Description: desc,
		Resolution:  res,
	}
}
