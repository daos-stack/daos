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

package config

import (
	"fmt"

	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
)

var (
	FaultUnknown = serverConfigFault(
		code.ServerConfigUnknown,
		"unknown control server error",
		"",
	)
	FaultBadConfig = serverConfigFault(
		code.ServerBadConfig,
		"insufficient information in configuration",
		"supply path to valid configuration file, use examples for reference",
	)
	FaultConfigNoPath = serverConfigFault(
		code.ServerNoConfigPath,
		"configuration file path not set",
		"supply the path to a server configuration file when restarting the control server with commandline option '-o'",
	)
	FaultConfigBadControlPort = serverConfigFault(
		code.ServerConfigBadControlPort,
		"invalid control port in configuration",
		"specify a nonzero control port in configuration ('port' parameter) and restart the control server",
	)
	FaultConfigBadAccessPoints = serverConfigFault(
		code.ServerConfigBadAccessPoints,
		"invalid list of access points in configuration",
		"only a single access point is currently supported, specify only one and restart the control server",
	)
	FaultConfigNoProvider = serverConfigFault(
		code.ServerConfigBadProvider,
		"provider not specified in configuration",
		"specify a valid network provider in configuration ('provider' parameter) and restart the control server",
	)
	FaultConfigNoServers = serverConfigFault(
		code.ServerConfigNoServers,
		"no DAOS IO Servers specified in configuration",
		"specify at least one IO Server configuration ('servers' list parameter) and restart the control server",
	)
	FaultConfigFaultDomainInvalid = serverConfigFault(
		code.ServerConfigFaultDomainInvalid,
		"invalid fault domain",
		"specify a valid fault domain ('fault_path' parameter) or callback script ('fault_cb' parameter) and restart the control server",
	)
	FaultConfigFaultCallbackNotFound = serverConfigFault(
		code.ServerConfigFaultCallbackNotFound,
		"fault domain callback script not found",
		"specify a valid fault domain callback script ('fault_cb' parameter) and restart the control server",
	)
	FaultConfigFaultCallbackBadPerms = serverConfigFault(
		code.ServerConfigFaultCallbackBadPerms,
		"fault domain callback cannot be executed",
		"ensure that permissions for the DAOS server user are properly set on the fault domain callback script ('fault_cb' parameter) and restart the control server",
	)
	FaultConfigBothFaultPathAndCb = serverConfigFault(
		code.ServerConfigBothFaultPathAndCb,
		"both fault domain and fault path are defined in the configuration",
		"remove either the fault domain ('fault_path' parameter) or callback script ('fault_cb' parameter) and restart the control server",
	)
	FaultConfigFaultCallbackEmpty = serverConfigFault(
		code.ServerConfigFaultCallbackEmpty,
		"fault domain callback executed but did not generate output",
		"specify a valid fault domain callback script ('fault_cb' parameter) and restart the control server",
	)
)

func FaultConfigDuplicateFabric(curIdx, seenIdx int) *fault.Fault {
	return serverConfigFault(
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
	return serverConfigFault(
		code.ServerConfigOverlappingBdevDeviceList,
		fmt.Sprintf("the bdev_list value in IO server %d overlaps with entries in server %d", curIdx, seenIdx),
		"ensure that each IO server has a unique set of bdev_list entries and restart",
	)
}

func FaultConfigInvalidNetDevClass(curIdx int, primaryDevClass, thisDevClass uint32, iface string) *fault.Fault {
	return serverConfigFault(
		code.ServerConfigInvalidNetDevClass,
		fmt.Sprintf("IO server %d specifies fabric_iface %q of class %q that conflicts with the primary server's device class %q",
			curIdx, iface, netdetect.DevClassName(thisDevClass), netdetect.DevClassName(primaryDevClass)),
		"ensure that each IO server specifies a fabric_iface with a matching device class and restart",
	)
}

func dupeValue(code code.Code, name string, curIdx, seenIdx int) *fault.Fault {
	return serverConfigFault(code,
		fmt.Sprintf("the %s value in IO server %d is a duplicate of server %d", name, curIdx, seenIdx),
		fmt.Sprintf("ensure that each IO server has a unique %s value and restart", name),
	)
}

// FaultConfigFaultCallbackFailed creates a Fault for the scenario where the
// fault domain callback failed with some error.
func FaultConfigFaultCallbackFailed(err error) *fault.Fault {
	return serverConfigFault(
		code.ServerConfigFaultCallbackFailed,
		fmt.Sprintf("fault domain callback script failed during execution: %s", err.Error()),
		"specify a valid fault domain callback script ('fault_cb' parameter) and restart the control server",
	)
}

func serverConfigFault(code code.Code, desc, res string) *fault.Fault {
	return &fault.Fault{
		Domain:      "serverconfig",
		Code:        code,
		Description: desc,
		Resolution:  res,
	}
}
