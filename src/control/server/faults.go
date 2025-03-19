//
// (C) Copyright 2020-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"fmt"
	"sort"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/dustin/go-humanize/english"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/server/engine"
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
	FaultPoolNoLabel = serverFault(
		code.ServerPoolNoLabel,
		"cannot create a pool without a pool label",
		"retry the operation with a label set",
	)
	FaultPoolHasContainers = serverFault(
		code.ServerPoolHasContainers,
		"cannot destroy a pool with existing containers",
		"retry the operation with the recursive flag set to remove containers along with the pool",
	)
	FaultHugepagesDisabled = serverFault(
		code.ServerHugepagesDisabled,
		"the use of hugepages has been disabled in the server config",
		"set false (or remove) disable_hugepages parameter in config and reformat storage, then retry the operation",
	)
)

func FaultPoolInvalidServiceReps(maxSvcReps uint32) *fault.Fault {
	return serverFault(
		code.ServerPoolInvalidServiceReps,
		fmt.Sprintf("pool service replicas number should be an odd number between 1 and %d", maxSvcReps),
		"retry the request with a valid number of pool service replicas",
	)
}

func FaultInstancesNotStopped(action string, rank ranklist.Rank) *fault.Fault {
	return serverFault(
		code.ServerInstancesNotStopped,
		fmt.Sprintf("%s not supported when rank %d is running", action, rank),
		fmt.Sprintf("retry %s operation after stopping rank %d", action, rank),
	)
}

func FaultPoolNvmeTooSmall(minTotal, minNVMe uint64) *fault.Fault {
	return serverFault(
		code.ServerPoolNvmeTooSmall,
		fmt.Sprintf("requested NVMe capacity too small (min %s per target)",
			humanize.IBytes(engine.NvmeMinBytesPerTarget)),
		fmt.Sprintf("retry the request with a pool size of at least %s, with at least %s NVMe",
			humanize.IBytes(minTotal+humanize.MiByte), humanize.IBytes(minNVMe+humanize.MiByte),
		),
	)
}

func FaultPoolScmTooSmall(minTotal, minSCM uint64) *fault.Fault {
	return serverFault(
		code.ServerPoolScmTooSmall,
		fmt.Sprintf("requested SCM capacity is too small (min %s per target)",
			humanize.IBytes(engine.ScmMinBytesPerTarget)),
		fmt.Sprintf("retry the request with a pool size of at least %s, with at least %s SCM",
			humanize.IBytes(minTotal+humanize.MiByte), humanize.IBytes(minSCM+humanize.MiByte),
		),
	)
}

func FaultPoolInvalidRanks(invalid []ranklist.Rank) *fault.Fault {
	rs := make([]string, len(invalid))
	for i, r := range invalid {
		rs[i] = r.String()
	}
	sort.Strings(rs)

	return serverFault(
		code.ServerPoolInvalidRanks,
		fmt.Sprintf("pool request contains invalid %s: %s",
			english.PluralWord(len(invalid), "rank", "ranks"), strings.Join(rs, ",")),
		"retry the request with a valid set of ranks",
	)
}

func FaultPoolInvalidNumRanks(req, avail int) *fault.Fault {
	return serverFault(
		code.ServerPoolInvalidNumRanks,
		fmt.Sprintf("pool request contains invalid number of ranks (requested: %d, available: %d)", req, avail),
		"retry the request with a valid number of ranks",
	)
}

func FaultPoolDuplicateLabel(dupe string) *fault.Fault {
	return serverFault(
		code.ServerPoolDuplicateLabel,
		fmt.Sprintf("pool label %q already exists in the system", dupe),
		"retry the request with a unique pool label",
	)
}

func FaultEngineNUMAImbalance(nodeMap map[int]int) *fault.Fault {
	return serverFault(
		code.ServerConfigEngineNUMAImbalance,
		fmt.Sprintf("uneven distribution of engines across NUMA nodes %v", nodeMap),
		"config requires an equal number of engines assigned to each NUMA node",
	)
}

func FaultScmUnmanaged(mntPoint string) *fault.Fault {
	return serverFault(
		code.ServerScmUnmanaged,
		fmt.Sprintf("the SCM mountpoint at %s is unavailable and can't be created/mounted", mntPoint),
		fmt.Sprintf("manually create %s and retry", mntPoint),
	)
}

func FaultWrongSystem(reqName, sysName string) *fault.Fault {
	return serverFault(
		code.ServerWrongSystem,
		fmt.Sprintf("request system does not match running system (%s != %s)", reqName, sysName),
		"retry the request with the correct system name",
	)
}

func FaultIncompatibleComponents(self, other *build.VersionedComponent) *fault.Fault {
	return serverFault(
		code.ServerIncompatibleComponents,
		fmt.Sprintf("components %s and %s are not compatible", self, other),
		"retry the request with compatible components",
	)
}

func FaultNoCompatibilityInsecure(self, other build.Version) *fault.Fault {
	return serverFault(
		code.ServerNoCompatibilityInsecure,
		fmt.Sprintf("versions %s and %s are not compatible in insecure mode", self, other),
		"enable certificates or use identical component versions",
	)
}

// FaultPoolMemRatioNoRoles indicates a fault when pool create request contains MD-on-SSD
// parameters but MD-on-SSD has not been enabled on the server.
var FaultPoolMemRatioNoRoles = serverFault(
	code.ServerPoolMemRatioNoRoles,
	"pool create request contains MD-on-SSD parameters but MD-on-SSD has not been enabled",
	"either remove MD-on-SSD-specific options from the command request or set bdev_roles in "+
		"server config file to enable MD-on-SSD")

func FaultBadFaultDomainLabels(faultPath, addr string, reqLabels, systemLabels []string) *fault.Fault {
	return serverFault(
		code.ServerBadFaultDomainLabels,
		fmt.Sprintf("labels in join request [%s] don't match system labels [%s] for server %s (fault path: %s)",
			strings.Join(reqLabels, ", "), strings.Join(systemLabels, ", "), addr, faultPath),
		"update the 'fault_path' or executable specified in 'fault_cb' in the affected server's configuration file to match the system labels",
	)
}

func FaultJoinReplaceEnabledPoolRank(rank ranklist.Rank, poolIDs ...string) *fault.Fault {
	return serverFault(
		code.ServerJoinReplaceEnabledPoolRank,
		fmt.Sprintf("rank %d is enabled on %s %s and cannot be replaced until excluded on all pools",
			rank, english.PluralWord(len(poolIDs), "pool", "pools"), strings.Join(poolIDs, ",")),
		"run dmg system exclude --rank=<rank> to manually exclude rank from all system pools then attempt dmg storage format --replace again",
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
