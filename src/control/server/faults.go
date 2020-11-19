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
	"github.com/daos-stack/daos/src/control/server/ioserver"
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

func serverFault(code code.Code, desc, res string) *fault.Fault {
	return &fault.Fault{
		Domain:      "server",
		Code:        code,
		Description: desc,
		Resolution:  res,
	}
}
