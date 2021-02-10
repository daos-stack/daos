//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package server

import (
	"fmt"
	"sort"
	"strings"

	"github.com/dustin/go-humanize"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/system"
)

var (
	FaultUnknown = serverFault(
		code.ServerUnknown,
		"unknown control server error",
		"",
	)
	FaultIommuDisabled = serverFault(
		code.ServerIommuDisabled,
		"no IOMMU detected while running as non-root user with NVMe devices",
		"enable IOMMU per the DAOS Admin Guide or run daos_server as root",
	)
	FaultVfioDisabled = serverFault(
		code.ServerVfioDisabled,
		"disable_vfio: true in config while running as non-root user with NVMe devices",
		"set disable_vfio: false or run daos_server as root",
	)
	FaultHarnessNotStarted = serverFault(
		code.ServerHarnessNotStarted,
		fmt.Sprintf("%s harness not started", build.DataPlaneName),
		"retry the operation or check server logs for more details",
	)
	FaultDataPlaneNotStarted = serverFault(
		code.ServerDataPlaneNotStarted,
		fmt.Sprintf("%s instance not started or not responding on dRPC", build.DataPlaneName),
		"retry the operation or check server logs for more details",
	)
	FaultPoolInvalidServiceReps = serverFault(
		code.ServerPoolInvalidServiceReps,
		fmt.Sprintf("pool service replicas number should be an odd number between 1 and %d", MaxPoolServiceReps),
		"retry the request with a valid number of pool service replicas",
	)
)

func FaultInstancesNotStopped(action string, rank system.Rank) *fault.Fault {
	return serverFault(
		code.ServerInstancesNotStopped,
		fmt.Sprintf("%s not supported when rank %d is running", action, rank),
		fmt.Sprintf("retry %s operation after stopping rank %d", action, rank),
	)
}

func FaultPoolNvmeTooSmall(reqBytes uint64, targetCount int) *fault.Fault {
	return serverFault(
		code.ServerPoolNvmeTooSmall,
		fmt.Sprintf("requested NVMe capacity (%s / %d) is too small (min %s per target)",
			humanize.Bytes(reqBytes), targetCount,
			humanize.IBytes(engine.NvmeMinBytesPerTarget)),
		fmt.Sprintf("NVMe capacity should be larger than %s",
			humanize.Bytes(engine.NvmeMinBytesPerTarget*uint64(targetCount))),
	)
}

func FaultPoolScmTooSmall(reqBytes uint64, targetCount int) *fault.Fault {
	return serverFault(
		code.ServerPoolScmTooSmall,
		fmt.Sprintf("requested SCM capacity (%s / %d) is too small (min %s per target)",
			humanize.Bytes(reqBytes), targetCount,
			humanize.IBytes(engine.ScmMinBytesPerTarget)),
		fmt.Sprintf("SCM capacity should be larger than %s",
			humanize.Bytes(engine.ScmMinBytesPerTarget*uint64(targetCount))),
	)
}

func FaultPoolInvalidRanks(invalid []system.Rank) *fault.Fault {
	rs := make([]string, len(invalid))
	for i, r := range invalid {
		rs[i] = r.String()
	}
	sort.Strings(rs)

	return serverFault(
		code.ServerPoolInvalidRanks,
		fmt.Sprintf("pool request contains invalid ranks: %s", strings.Join(rs, ",")),
		"retry the request with a valid set of ranks",
	)
}

func FaultPoolDuplicateLabel(dupe string) *fault.Fault {
	return serverFault(
		code.ServerPoolDuplicateLabel,
		fmt.Sprintf("pool label %q already exists in the system", dupe),
		"retry the request with a unique pool label",
	)
}

func FaultInsufficientFreeHugePages(free, requested int) *fault.Fault {
	return serverFault(
		code.ServerInsufficientFreeHugePages,
		fmt.Sprintf("requested %d hugepages; got %d", requested, free),
		"reboot the system or manually clear /dev/hugepages as appropriate",
	)
}

func FaultScmUnmanaged(mntPoint string) *fault.Fault {
	return serverFault(
		code.ServerScmUnmanaged,
		fmt.Sprintf("the SCM mountpoint at %s is unavailable and can't be created/mounted", mntPoint),
		fmt.Sprintf("manually create %s or remove --recreate-superblocks from the server arguments", mntPoint),
	)
}

func FaultBdevNotFound(bdevs []string) *fault.Fault {
	return serverFault(
		code.ServerBdevNotFound,
		fmt.Sprintf("NVMe SSD%s %v not found", common.Pluralise("", len(bdevs)), bdevs),
		fmt.Sprintf("check SSD%s %v that are specified in server config exist", common.Pluralise("", len(bdevs)), bdevs),
	)
}

func FaultWrongSystem(reqName, sysName string) *fault.Fault {
	return serverFault(
		code.ServerWrongSystem,
		fmt.Sprintf("request system does not match running system (%s != %s)", reqName, sysName),
		"retry the request with the correct system name",
	)
}

func serverFault(code code.Code, desc, res string) *fault.Fault {
	return &fault.Fault{
		Domain:      "server",
		Code:        code,
		Description: desc,
		Resolution:  res,
	}
}
