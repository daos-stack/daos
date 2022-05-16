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

// FaultScmNotInterleaved represents an error where PMem regions exist in non-interleaved
// mode, this is unsupported.
var FaultScmNotInterleaved = storageFault(code.ScmNotInterleaved,
	"PMem regions are in AppDirect non-interleaved mode",
	"Rerun the command first with the --reset option and then without to recreate the "+
		"PMem regions in the recommended mode")

// FaultScmNoModules represents an error where no PMem modules exist.
var FaultScmNoModules = storageFault(code.ScmNoModules,
	"No PMem modules exist on storage server",
	"Install PMem modules and retry command")

// FaultScmNamespacesNrMismatch creates a Fault for the case where the number of SCM namespaces
// doesn't match the number expected.
func FaultScmNamespacesNrMismatch(nrExpPerNUMA, nrNUMA, nrExisting uint) *fault.Fault {
	return storageFault(
		code.ScmNamespacesNrMismatch,
		fmt.Sprintf("want %d PMem namespaces but got %d, %d PMem namespaces expected per "+
			"NUMA node and found %d nodes", (nrExpPerNUMA*nrNUMA), nrExisting,
			nrExpPerNUMA, nrNUMA),
		"Rerun the command first with the --reset option and then without to recreate the "+
			"required number of PMem regions")
}

// FaultBdevNotFound creates a Fault for the case where no NVMe storage devices
// match expected PCI addresses.
func FaultBdevNotFound(bdevs ...string) *fault.Fault {
	return storageFault(
		code.BdevNotFound,
		fmt.Sprintf("NVMe SSD%s %v not found", common.Pluralise("", len(bdevs)), bdevs),
		fmt.Sprintf("check SSD%s %v that are specified in server config exist",
			common.Pluralise("", len(bdevs)), bdevs),
	)
}

// FaultBdevAccelEngineUnknown creates a Fault when an unrecognized acceleration engine setting is
// detected.
func FaultBdevAccelEngineUnknown(input string, options ...string) *fault.Fault {
	return storageFault(
		code.BdevAccelEngineUnknown,
		fmt.Sprintf("unknown acceleration engine setting %q", input),
		fmt.Sprintf("supported settings are %v, update server config file and restart daos_server",
			options))
}

// FaultBdevAccelOptUnknown creates a Fault when an unrecognized acceleration option is detected.
func FaultBdevAccelOptionUnknown(input string, options ...string) *fault.Fault {
	return storageFault(
		code.BdevAccelOptionUnknown,
		fmt.Sprintf("unknown acceleration option %q", input),
		fmt.Sprintf("supported options are %v, update server config file and restart daos_server",
			options))
}

func storageFault(code code.Code, desc, res string) *fault.Fault {
	return &fault.Fault{
		Domain:      "storage",
		Code:        code,
		Description: desc,
		Resolution:  res,
	}
}
