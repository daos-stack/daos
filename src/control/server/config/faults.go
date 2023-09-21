//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package config

import (
	"fmt"

	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/dustin/go-humanize"
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
		"specify a positive non-zero network port in configuration ('port' parameter) and restart the control server",
	)
	FaultConfigBadTelemetryPort = serverConfigFault(
		code.ServerConfigBadTelemetryPort,
		"invalid telemetry port in configuration",
		"specify a positive non-zero network port in configuration ('telemetry_port' parameter) and restart the control server",
	)
	FaultConfigBadAccessPoints = serverConfigFault(
		code.ServerConfigBadAccessPoints,
		"invalid list of access points in configuration",
		"'access_points' must contain resolvable addresses; fix the configuration and restart the control server",
	)
	FaultConfigEvenAccessPoints = serverConfigFault(
		code.ServerConfigEvenAccessPoints,
		"non-odd number of access points in configuration",
		"'access_points' must contain an odd number (e.g. 1, 3, 5, etc.) of addresses; fix the configuration and restart the control server",
	)
	FaultConfigNoProvider = serverConfigFault(
		code.ServerConfigBadProvider,
		"provider not specified in server configuration",
		"specify a valid network provider in configuration ('provider' parameter) and restart the control server",
	)
	FaultConfigNoEngines = serverConfigFault(
		code.ServerConfigNoEngines,
		"no DAOS IO Engines specified in configuration",
		"specify at least one IO Engine configuration ('engines' list parameter) and restart the control server",
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
	FaultConfigTooManyLayersInFaultDomain = serverConfigFault(
		code.ServerConfigFaultDomainTooManyLayers,
		"the fault domain path may have a maximum of 2 levels below the root",
		"update either the fault domain ('fault_path' parameter) or callback script ('fault_cb' parameter) and restart the control server",
	)
	FaultConfigHugepagesDisabled = serverConfigFault(
		code.ServerConfigHugepagesDisabled,
		"hugepages cannot be disabled if bdevs have been specified in config",
		"remove nr_hugepages parameter from config to have the value automatically calculated",
	)
	FaultConfigVMDSettingDuplicate = serverConfigFault(
		code.ServerConfigVMDSettingDuplicate,
		"enable_vmd and disable_vmd parameters both specified in config",
		"remove legacy enable_vmd parameter from config",
	)
	FaultConfigControlMetadataNoPath = serverConfigFault(
		code.ServerConfigControlMetadataNoPath,
		"using a control_metadata device requires a path to use as the mount point",
		"add a valid 'path' to the 'control_metadata' section of the config",
	)
	FaultConfigEngineBdevRolesMismatch = serverConfigFault(
		code.ServerConfigEngineBdevRolesMismatch,
		"md-on-ssd bdev roles have been set in some but not all engine configs",
		"set bdev roles on all engines or remove all bdev role assignments in config",
	)
	FaultConfigSysRsvdZero = serverConfigFault(
		code.ServerConfigSysRsvdZero,
		"`system_ram_reserved` is set to zero in server config",
		"set `system_ram_reserved` to a positive integer value in config",
	)
)

func FaultConfigDuplicateFabric(curIdx, seenIdx int) *fault.Fault {
	return serverConfigFault(
		code.ServerConfigDuplicateFabric,
		fmt.Sprintf("the fabric configuration in I/O Engine %d is a duplicate of I/O Engine %d", curIdx, seenIdx),
		"ensure that each I/O Engine has a unique combination of provider,fabric_iface,fabric_iface_port and restart",
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

func FaultConfigScmDiffClass(curIdx, seenIdx int) *fault.Fault {
	return serverConfigFault(
		code.ServerConfigScmDiffClass,
		fmt.Sprintf("the SCM class in I/O Engine %d is different from I/O Engine %d",
			curIdx, seenIdx),
		"ensure that each I/O Engine has a single SCM tier with the same class and restart",
	)
}

func FaultConfigOverlappingBdevDeviceList(curIdx, seenIdx int) *fault.Fault {
	return serverConfigFault(
		code.ServerConfigOverlappingBdevDeviceList,
		fmt.Sprintf("the bdev_list value in I/O Engine %d overlaps with entries in server %d", curIdx, seenIdx),
		"ensure that each I/O Engine has a unique set of bdev_list entries and restart",
	)
}

func FaultConfigBdevCountMismatch(curIdx, curCount, seenIdx, seenCount int) *fault.Fault {
	return serverConfigFault(
		code.ServerConfigBdevCountMismatch,
		fmt.Sprintf("the total number of bdev_list entries in all tiers is not equal across engines, engine %d has %d but engine %d has %d",
			curIdx, curCount, seenIdx, seenCount),
		"ensure that each I/O Engine has an equal number of total bdev_list entries and restart",
	)
}

func FaultConfigTargetCountMismatch(curIdx, curCount, seenIdx, seenCount int) *fault.Fault {
	return serverConfigFault(
		code.ServerConfigTargetCountMismatch,
		fmt.Sprintf("the target count is not equal across engines, engine %d has %d but engine %d has %d",
			curIdx, curCount, seenIdx, seenCount),
		"ensure that each I/O Engine has an equal integer value for targets parameter and restart",
	)
}

func FaultConfigHelperStreamCountMismatch(curIdx, curCount, seenIdx, seenCount int) *fault.Fault {
	return serverConfigFault(
		code.ServerConfigHelperStreamCountMismatch,
		fmt.Sprintf("the helper stream count is not equal across engines, engine %d has %d but engine %d has %d",
			curIdx, curCount, seenIdx, seenCount),
		"ensure that each I/O Engine has an equal integer value for nr_xs_helpers parameter and restart",
	)
}

func FaultConfigInvalidNetDevClass(curIdx int, primaryDevClass, thisDevClass hardware.NetDevClass, iface string) *fault.Fault {
	return serverConfigFault(
		code.ServerConfigInvalidNetDevClass,
		fmt.Sprintf("I/O Engine %d specifies fabric_iface %q of class %q that conflicts with the primary server's device class %q",
			curIdx, iface, thisDevClass, primaryDevClass),
		"ensure that each I/O Engine specifies a fabric_iface with a matching device class and restart",
	)
}

func dupeValue(code code.Code, name string, curIdx, seenIdx int) *fault.Fault {
	return serverConfigFault(code,
		fmt.Sprintf("the %s value in I/O Engine %d is a duplicate of I/O Engine %d", name, curIdx, seenIdx),
		fmt.Sprintf("ensure that each I/O Engine has a unique %s value and restart", name),
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

// FaultConfigFaultCallbackInsecure creates a fault for the scenario where the
// fault domain callback path doesn't meet security requirements.
func FaultConfigFaultCallbackInsecure(requiredDir string) *fault.Fault {
	return serverConfigFault(
		code.ServerConfigFaultCallbackInsecure,
		"fault domain callback script does not meet security requirements",
		fmt.Sprintf("ensure that the 'fault_cb' path is under the parent directory %q, "+
			"not a symbolic link, does not have the setuid bit set, and does not have "+
			"write permissions for non-owners", requiredDir),
	)
}

// FaultConfigNrHugepagesOutOfRange creates a fault for the scenario where the number of configured
// huge pages is smaller than zero or larger than the maximum value allowed.
func FaultConfigNrHugepagesOutOfRange(req, max int) *fault.Fault {
	return serverConfigFault(
		code.ServerConfigNrHugepagesOutOfRange,
		fmt.Sprintf("number of hugepages specified (%d) is out of range (0 - %d)", req, max),
		fmt.Sprintf("specify a nr_hugepages value between 0 and %d", max),
	)
}

// FaultConfigRamdiskOverMaxMem indicates that the tmpfs size requested in config is larger than
// maximum allowed.
func FaultConfigRamdiskOverMaxMem(confSize, ramSize, memRamdiskMin uint64) *fault.Fault {
	return serverConfigFault(
		code.ServerConfigRamdiskOverMaxMem,
		fmt.Sprintf("configured scm tmpfs size %s is larger than the maximum (%s) that "+
			"total system memory (RAM) will allow", humanize.IBytes(confSize),
			humanize.IBytes(ramSize)),
		fmt.Sprintf("remove the 'scm_size' parameter so it can be automatically set "+
			"or manually set to a value between %s and %s in the config file",
			humanize.IBytes(memRamdiskMin), humanize.IBytes(ramSize)),
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
