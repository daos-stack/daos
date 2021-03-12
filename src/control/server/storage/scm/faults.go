//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package scm

import (
	"fmt"

	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
)

var (
	// FaultUnknown represents an unspecified SCM error.
	FaultUnknown = scmFault(
		code.ScmUnknown, "unknown scm error", "",
	)

	// FaultDiscoveryFailed represents an SCM discovery failure.
	FaultDiscoveryFailed = scmFault(
		code.ScmDiscoveryFailed, "module discovery failed", "",
	)

	// FaultFormatInvalidSize represents an error where an invalid SCM format size
	// was requested.
	FaultFormatInvalidSize = scmFault(
		code.ScmFormatInvalidSize, "format request must specify a size greater than 0", "",
	)

	// FaultFormatInvalidDeviceCount represents an error where an invalid number
	// of SCM devices was requested during format.
	FaultFormatInvalidDeviceCount = scmFault(
		code.ScmFormatInvalidDeviceCount, "format request must have exactly 1 dcpm device", "",
	)

	// FaultFormatMissingMountpoint represents an error where a mountpoint
	// was missing from an SCM format request.
	FaultFormatMissingMountpoint = scmFault(
		code.ScmFormatMissingMountpoint, "format request must specify mountpoint", "",
	)

	// FaultFormatMissingParam represents an error where a parameter was missing
	// from a format request.
	FaultFormatMissingParam = scmFault(
		code.ScmFormatMissingParam, "format request must have ramdisk or dcpm parameter", "",
	)

	// FaultFormatConflictingParam represents an error where a format request
	// had conflicting parameters.
	FaultFormatConflictingParam = scmFault(
		code.ScmFormatConflictingParam,
		"format request must not have both ramdisk and dcpm parameters", "",
	)

	// FaultFormatNoReformat represents an error where a format was requested
	// on SCM storage that was already formatted.
	FaultFormatNoReformat = scmFault(
		code.StorageAlreadyFormatted,
		"format request for already-formatted storage and reformat not specified",
		"retry the operation with reformat option to overwrite existing format",
	)

	// FaultDeviceAlreadyMounted represents an error where a format was requested
	// on SCM storage that was already mounted on the system.
	FaultDeviceAlreadyMounted = scmFault(
		code.StorageDeviceAlreadyMounted,
		"request included already-mounted device",
		"unmount the device and retry the operation",
	)

	// FaultTargetAlreadyMounted represents an error where a format was requested
	// on an SCM storage target that was already mounted on the system.
	FaultTargetAlreadyMounted = scmFault(
		code.StorageTargetAlreadyMounted,
		"request included already-mounted mount target (cannot double-mount)",
		"unmount the target and retry the operation",
	)

	// FaultMissingNdctl represents an error where the ndctl SCM management tool
	// is not installed on the system.
	FaultMissingNdctl = scmFault(
		code.MissingSoftwareDependency,
		"ndctl utility not found", "install the ndctl software for your OS",
	)

	// FaultDuplicateDevices represents an error where a user provided duplicate
	// device IDs in an input.
	FaultDuplicateDevices = scmFault(code.ScmDuplicatesInDeviceList,
		"duplicates in SCM device list",
		"check your device list and try again")

	// FaultNoFilterMatch represents an error where no modules were found that
	// matched user-provided filter criteria.
	FaultNoFilterMatch = scmFault(code.ScmNoDevicesMatchFilter,
		"no SCM modules matched the filter criteria",
		"adjust or relax the filters and try again")
)

func FaultIpmctlBadVersion(version string) *fault.Fault {
	return scmFault(
		code.BadVersionSoftwareDependency,
		fmt.Sprintf("DAOS will not work with ipmctl package version %s", version),
		"upgrade system ipmctl package to meet version requirements",
	)
}

// FaultFormatMissingDevice creates a Fault for the case where a requested
// device was not found.
func FaultFormatMissingDevice(device string) *fault.Fault {
	return scmFault(
		code.ScmFormatMissingDevice,
		fmt.Sprintf("configured SCM device %s does not exist", device),
		"check the configured value and/or perform the SCM preparation procedure",
	)
}

func scmFault(code code.Code, desc, res string) *fault.Fault {
	return &fault.Fault{
		Domain:      "scm",
		Code:        code,
		Description: desc,
		Resolution:  res,
	}
}
