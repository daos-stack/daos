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
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/system"
)

var (
	FaultUnknown = serverFault(
		code.ServerUnknown,
		"unknown control server error", "")
	FaultBadConfig = serverFault(
		code.ServerBadConfig,
		"insufficient information in configuration",
		"supply path to valid configuration file, use examples for reference",
	)
	FaultConfigNoPath = serverFault(
		code.ServerNoConfigPath,
		"configuration file path not set",
		"supply the path to a server configuration file when restarting the control server with commandline option '-o'",
	)
	FaultConfigBadControlPort = serverFault(
		code.ServerConfigBadControlPort,
		"invalid control port in configuration",
		"specify a nonzero control port in configuration ('port' parameter) and restart the control server",
	)
	FaultConfigBadAccessPoints = serverFault(
		code.ServerConfigBadAccessPoints,
		"invalid list of access points in configuration",
		"only a single access point is currently supported, specify only one and restart the control server",
	)
	FaultConfigNoProvider = serverFault(
		code.ServerConfigBadProvider,
		"provider not specified in configuration",
		"specify a valid network provider in configuration ('provider' parameter) and restart the control server",
	)
	FaultConfigNoServers = serverFault(
		code.ServerConfigNoServers,
		"no DAOS IO Servers specified in configuration",
		"specify at least one IO Server configuration ('servers' list parameter) and restart the control server",
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
	FaultConfigFaultDomainInvalid = serverFault(
		code.ServerConfigFaultDomainInvalid,
		"invalid fault domain",
		"specify a valid fault domain ('fault_path' parameter) or callback script ('fault_cb' parameter) and restart the control server",
	)
	FaultConfigFaultCallbackNotFound = serverFault(
		code.ServerConfigFaultCallbackNotFound,
		"fault domain callback script not found",
		"specify a valid fault domain callback script ('fault_cb' parameter) and restart the control server",
	)
	FaultConfigFaultCallbackBadPerms = serverFault(
		code.ServerConfigFaultCallbackBadPerms,
		"fault domain callback cannot be executed",
		"ensure that permissions for the DAOS server user are properly set on the fault domain callback script ('fault_cb' parameter) and restart the control server",
	)
	FaultConfigBothFaultPathAndCb = serverFault(
		code.ServerConfigBothFaultPathAndCb,
		"both fault domain and fault path are defined in the configuration",
		"remove either the fault domain ('fault_path' parameter) or callback script ('fault_cb' parameter) and restart the control server",
	)
	FaultConfigFaultCallbackEmpty = serverFault(
		code.ServerConfigFaultCallbackEmpty,
		"fault domain callback executed but did not generate output",
		"specify a valid fault domain callback script ('fault_cb' parameter) and restart the control server",
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
		fmt.Sprintf("requested NVMe capacity (%s) is too small (min %s)",
			humanize.IBytes(reqBytes),
			humanize.IBytes(ioserver.NvmeMinBytesPerTarget*uint64(targetCount))),
		fmt.Sprintf("NVMe capacity should be larger than %s",
			humanize.IBytes(ioserver.NvmeMinBytesPerTarget*uint64(targetCount))),
	)
}

func FaultPoolScmTooSmall(reqBytes uint64, targetCount int) *fault.Fault {
	return serverFault(
		code.ServerPoolScmTooSmall,
		fmt.Sprintf("requested SCM capacity (%s) is too small (min %s)",
			humanize.IBytes(reqBytes),
			humanize.IBytes(ioserver.ScmMinBytesPerTarget*uint64(targetCount))),
		fmt.Sprintf("SCM capacity should be larger than %s",
			humanize.IBytes(ioserver.ScmMinBytesPerTarget*uint64(targetCount))),
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

func FaultConfigDuplicateFabric(curIdx, seenIdx int) *fault.Fault {
	return serverFault(
		code.ServerConfigDuplicateFabric,
		fmt.Sprintf("the fabric configuration in IO server %d is a duplicate of server %d", curIdx, seenIdx),
		"ensure that each IO server has a unique combination of provider,fabric_iface,fabric_iface_port and restart",
	)
}

func FaultConfigDuplicateLogFile(curIdx, seenIdx int) *fault.Fault {
	return dupeValue(
		code.ServerConfigDuplicateLogFile, "log_file", curIdx, seenIdx,
	)
}

func FaultConfigDuplicateScmMount(curIdx, seenIdx int) *fault.Fault {
	return dupeValue(
		code.ServerConfigDuplicateScmMount, "scm_mount", curIdx, seenIdx,
	)
}

func FaultConfigDuplicateScmDeviceList(curIdx, seenIdx int) *fault.Fault {
	return dupeValue(
		code.ServerConfigDuplicateScmDeviceList, "scm_list", curIdx, seenIdx,
	)
}

func FaultConfigOverlappingBdevDeviceList(curIdx, seenIdx int) *fault.Fault {
	return serverFault(
		code.ServerConfigOverlappingBdevDeviceList,
		fmt.Sprintf("the bdev_list value in IO server %d overlaps with entries in server %d", curIdx, seenIdx),
		"ensure that each IO server has a unique set of bdev_list entries and restart",
	)
}

func FaultConfigInvalidNetDevClass(curIdx int, primaryDevClass, thisDevClass uint32, iface string) *fault.Fault {
	return serverFault(
		code.ServerConfigInvalidNetDevClass,
		fmt.Sprintf("IO server %d specifies fabric_iface %q of class %q that conflicts with the primary server's device class %q",
			curIdx, iface, netdetect.DevClassName(thisDevClass), netdetect.DevClassName(primaryDevClass)),
		"ensure that each IO server specifies a fabric_iface with a matching device class and restart",
	)
}

func dupeValue(code code.Code, name string, curIdx, seenIdx int) *fault.Fault {
	return serverFault(code,
		fmt.Sprintf("the %s value in IO server %d is a duplicate of server %d", name, curIdx, seenIdx),
		fmt.Sprintf("ensure that each IO server has a unique %s value and restart", name),
	)
}

// FaultConfigFaultCallbackFailed creates a Fault for the scenario where the
// fault domain callback failed with some error.
func FaultConfigFaultCallbackFailed(err error) *fault.Fault {
	return serverFault(
		code.ServerConfigFaultCallbackFailed,
		fmt.Sprintf("fault domain callback script failed during execution: %s", err.Error()),
		"specify a valid fault domain callback script ('fault_cb' parameter) and restart the control server",
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
