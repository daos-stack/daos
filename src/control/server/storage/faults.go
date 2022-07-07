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
func FaultScmNotInterleaved(sockID int) *fault.Fault {
	return storageFault(
		code.ScmBadRegion,
		fmt.Sprintf("PMem region on socket %d is in non-interleaved mode which is unsupported", sockID),
		"Remove and recreate region in AppDirect interleaved mode")
}

func FaultScmNotHealthy(sockID int) *fault.Fault {
	return storageFault(
		code.ScmBadRegion,
		fmt.Sprintf("PMem region on socket %d is unhealthy", sockID),
		"Check persistent memory modules are not faulty and then remove and recreate region")
}

func FaultScmPartialCapacity(sockID int) *fault.Fault {
	return storageFault(
		code.ScmBadRegion,
		fmt.Sprintf("PMem region on socket %d only has partial capacity free", sockID),
		"Creating namespaces on regions with only partial capacity available is unsupported, remove"+
			"namespaces and try again")
}

func FaultScmUnknownMemoryMode(sockID int) *fault.Fault {
	return storageFault(
		code.ScmBadRegion,
		fmt.Sprintf("PMem region on socket %d has an unsupported persistent memory type", sockID),
		"Remove and recreate region in AppDirect interleaved mode")
}

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

// FaultBdevConfigTypeMismatch represents an error where an incompatible mix of emulated and
// non-emulated NVMe devices are present in the storage config.
var FaultBdevConfigTypeMismatch = storageFault(code.BdevConfigTypeMismatch,
	"A mix of emulated and non-emulated NVMe devices are specified in config",
	"Change config tiers to specify either emulated or non-emulated NVMe devices, but not a mix of both")

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
