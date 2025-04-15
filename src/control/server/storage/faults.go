//
// (C) Copyright 2022-2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"fmt"

	"github.com/dustin/go-humanize"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
)

const recreateRegionsStr = "Remove regions (and any namespaces) by running the reset subcommand, reboot, then run the prepare subcommand again to recreate regions in AppDirect interleaved mode, reboot and then run the prepare subcommand one more time to create the PMem namespaces"

// FaultScmNotInterleaved creates a fault for the case where the PMem region is in non-interleaved
// mode, this is unsupported.
func FaultScmNotInterleaved(sockID uint) *fault.Fault {
	return storageFault(
		code.ScmBadRegion,
		fmt.Sprintf("PMem region on socket %d is in non-interleaved mode which is "+
			"unsupported", sockID),
		recreateRegionsStr)
}

// FaultScmNotHealthy creates a fault for the case where the PMem region is in an unhealthy state.
func FaultScmNotHealthy(sockID uint) *fault.Fault {
	return storageFault(
		code.ScmBadRegion,
		fmt.Sprintf("PMem region on socket %d is unhealthy", sockID),
		fmt.Sprintf("Refer to the ipmctl instructions in the troubleshooting section of "+
			"the DAOS admin guide to check PMem module health and replace faulty PMem "+
			"modules. %s", recreateRegionsStr))
}

// FaultScmPartialCapacity creates a fault for the case where the PMem region has only partial
// free capacity, this is unsupported.
func FaultScmPartialCapacity(sockID uint) *fault.Fault {
	return storageFault(
		code.ScmBadRegion,
		fmt.Sprintf("PMem region on socket %d only has partial capacity free", sockID),
		"Creating namespaces on regions with partial free-capacity is unsupported, remove "+
			"namespaces by running the command with --reset and then without --reset, no "+
			"reboot should be required")
}

// FaultScmUnknownMemoryMode creates a Fault for the case where the PMem region has an unsupported
// persistent memory type (not AppDirect).
func FaultScmUnknownMemoryMode(sockID uint) *fault.Fault {
	return storageFault(
		code.ScmBadRegion,
		fmt.Sprintf("PMem region on socket %d has an unsupported persistent memory type",
			sockID),
		recreateRegionsStr)
}

// FaultScmInvalidPMem creates a fault for the case where PMem validation has failed.
func FaultScmInvalidPMem(msg string) *fault.Fault {
	return storageFault(
		code.ScmInvalidPMem,
		"PMem is in an invalid state: "+msg,
		recreateRegionsStr)
}

// FaultRamdiskLowMem indicates that total RAM is insufficient to support given configuration.
func FaultRamdiskLowMem(memType string, confRamdiskSize, memNeed, memHave uint64) *fault.Fault {
	return storageFault(
		code.ScmRamdiskLowMem,
		fmt.Sprintf("%s memory (RAM) insufficient for configured %s ram-disk size, want "+
			"%s RAM but only have %s", memType, humanize.IBytes(confRamdiskSize),
			humanize.IBytes(memNeed), humanize.IBytes(memHave)),
		"Reduce engine targets or the system_ram_reserved values in server config "+
			"file if reducing the requested amount of RAM is not possible")
}

// FaultRamdiskBadSize indicates that the already-mounted ramdisk is out
// of spec with the calculated ramdisk size for the engine.
func FaultRamdiskBadSize(existingSize, calcSize uint64) *fault.Fault {
	return storageFault(
		code.ScmRamdiskBadSize,
		fmt.Sprintf("already-mounted ramdisk size %s is too far from optimal size of %s",
			humanize.IBytes(existingSize), humanize.IBytes(calcSize)),
		fmt.Sprintf("unmount the ramdisk and allow DAOS to manage it, or remount with size %s",
			humanize.IBytes(calcSize)))
}

// FaultConfigRamdiskUnderMinMem indicates that the tmpfs size requested in config is less than
// minimum allowed.
func FaultConfigRamdiskUnderMinMem(confSize, memRamdiskMin uint64) *fault.Fault {
	return storageFault(
		code.ServerConfigRamdiskUnderMinMem,
		fmt.Sprintf("configured ram-disk size %s is lower than the minimum (%s) required "+
			"for SCM", humanize.IBytes(confSize), humanize.IBytes(memRamdiskMin)),
		fmt.Sprintf("remove the 'scm_size' parameter so it can be automatically set "+
			"or manually set to a value above %s in the config file",
			humanize.IBytes(memRamdiskMin)),
	)
}

// FaultDeviceWithFsNoMountpoint creates a Fault for the case where a mount device is missing
// respective target location.
func FaultDeviceWithFsNoMountpoint(dev, tgt string) *fault.Fault {
	return storageFault(
		code.StorageDeviceWithFsNoMountpoint,
		fmt.Sprintf("filesystem exists on device %s but mount-point path %s does not exist",
			dev, tgt),
		"check the mount-point path exists and if not create it before trying again",
	)
}

var (
	// FaultTargetAlreadyMounted represents an error where the target was already mounted.
	FaultTargetAlreadyMounted = storageFault(
		code.StorageTargetAlreadyMounted,
		"request included already-mounted mount target (cannot double-mount)",
		"unmount the target and retry the operation")

	// FaultScmNoPMem represents an error where no PMem modules exist.
	FaultScmNoPMem = storageFault(
		code.ScmNoPMem,
		"No PMem modules exist on storage server", "Install PMem modules and retry command")

	// FaultScmConfigTierMissing indicates a Fault when no scm tier is present in engine
	// storage config.
	FaultScmConfigTierMissing = storageFault(
		code.ScmConfigTierMissing,
		"missing scm storage tier in engine storage config",
		"add a scm tier in the first position of the engine storage tiers list in server config file and "+
			"restart daos_server")

	// FaultBdevConfigTierTypeMismatch represents an error where an incompatible mix of
	// emulated and non-emulated NVMe devices are present in the storage config.
	FaultBdevConfigTierTypeMismatch = storageFault(
		code.BdevConfigTierTypeMismatch,
		"bdev tiers found with both emulated and non-emulated NVMe types specified in config",
		"change config tiers to specify either emulated or non-emulated NVMe devices, but not a mix of both")

	// FaultBdevConfigRolesWithDCPM indicates a Fault when bdev roles are specified with DCPM
	// SCM class.
	FaultBdevConfigRolesWithDCPM = storageFault(
		code.BdevConfigRolesWithDCPM,
		"MD-on-SSD roles has been specified in config with scm class set to dcpm",
		"'bdev_roles' are only supported if the scm tier is of class ram so change dcpm tier to ram or "+
			"remove role assignments from bdev tiers then restart daos_server after updating server "+
			"config file")

	// FaultBdevConfigRolesMissing indicates a Fault when bdev roles are specified on some but
	// not all bdev tiers.
	FaultBdevConfigRolesMissing = storageFault(
		code.BdevConfigRolesMissing,
		"bdev tier MD-on-SSD roles have been specified on some but not all bdev tiers in config",
		"set 'bdev_roles' on all bdev tiers in server config file then restart daos_server")

	// FaultBdevConfigRolesWalDataNoMeta indicates an invalid configuration where WAL and Data
	// roles are specified on a bdev tier but not Meta.
	FaultBdevConfigRolesWalDataNoMeta = storageFault(
		code.BdevConfigRolesWalDataNoMeta,
		"WAL and Data MD-on-SSD roles have been specified on a bdev tier without Meta role",
		"set 'bdev_roles` on all bdev tiers in server config file but avoid the unsupported WAL+Data "+
			"combination then restart daos_server")

	// FaultBdevConfigMultiTiersWithoutRoles indicates a Fault when multiple bdev tiers exist
	// but no roles are specified.
	FaultBdevConfigMultiTiersWithoutRoles = storageFault(
		code.BdevConfigMultiTierWithoutRoles,
		"multiple bdev tiers but MD-on-SSD roles have not been specified",
		"set 'bdev_roles` on all bdev tiers or use only a single bdev tier in server config file then "+
			"restart daos_server")

	// FaultBdevConfigBadNrTiersWithRoles indicates a Fault when an invalid number of bdev tiers
	// exist when roles are specified.
	FaultBdevConfigBadNrTiersWithRoles = storageFault(
		code.BdevConfigBadNrTiersWithRoles,
		"only 1, 2 or 3 bdev tiers are supported when MD-on-SSD roles are specified",
		"reduce the number of bdev tiers to 3 or less in server config file then restart daos_server")

	// FaultBdevConfigControlMetadataNoRoles indicates a fault when control_metadata path has
	// been specified in server config file but MD-on-SSD has not been enabled.
	FaultBdevConfigControlMetadataNoRoles = storageFault(
		code.BdevConfigControlMetadataNoRoles,
		"using 'control_metadata.path' requires MD-on-SSD roles",
		"set 'bdev_roles' on bdev tiers in the engine storage section of the server config file then "+
			"restart daos_server")

	// FaultBdevConfigRolesNoControlMetadata indicates a fault when control_metadata path
	// has not been specified in server config file but MD-on-SSD has been enabled.
	FaultBdevConfigRolesNoControlMetadata = storageFault(
		code.BdevConfigRolesNoControlMetadata,
		"'control_metadata.path' is required when MD-on-SSD roles are specified",
		"set 'control_metadata.path' in the engine storage section of the server config file then "+
			"restart daos_server")
)

// FaultBdevConfigBadNrRoles creates a Fault when an unexpected number of roles have been assigned
// to bdev tiers.
func FaultBdevConfigBadNrRoles(role string, gotNr, wantNr int) *fault.Fault {
	return storageFault(
		code.BdevConfigRolesBadNr,
		fmt.Sprintf("found %d %s tiers, wanted %d", gotNr, role, wantNr),
		fmt.Sprintf("assign %s role to %d tiers in server config file then restart daos_server",
			role, wantNr))
}

// FaultBdevNotFound creates a Fault for the case where no NVMe storage devices match expected PCI
// addresses. VMD addresses are expected to have backing devices.
func FaultBdevNotFound(vmdEnabled bool, bdevs ...string) *fault.Fault {
	msg := fmt.Sprintf("NVMe SSD%s", common.Pluralise("", len(bdevs)))
	if vmdEnabled {
		msg = "backing devices for VMDs"
	}

	return storageFault(
		code.BdevNotFound,
		fmt.Sprintf("%s %v not found", msg, bdevs),
		fmt.Sprintf("check %s %v that are specified in server config exist", msg, bdevs),
	)
}

// FaultBdevAccelEngineUnknown creates a Fault when an unrecognized acceleration engine setting is
// detected.
func FaultBdevAccelEngineUnknown(input string, options ...string) *fault.Fault {
	return storageFault(
		code.BdevAccelEngineUnknown,
		fmt.Sprintf("unknown acceleration engine setting %q", input),
		fmt.Sprintf("supported settings are %v, update server config file then restart daos_server",
			options))
}

// FaultBdevConfigOptFlagUnknown creates a Fault when an unrecognized option flag (string) is detected
// in the engine storage section of the config file.
func FaultBdevConfigOptFlagUnknown(input string, options ...string) *fault.Fault {
	return storageFault(
		code.BdevConfigOptFlagUnknown,
		fmt.Sprintf("unknown option flag given: %q", input),
		fmt.Sprintf("supported options are %v, update server config file then restart daos_server",
			options))
}

var (
	// FaultBdevNonRootVFIODisable indicates VFIO has been disabled but user is not privileged.
	FaultBdevNonRootVFIODisable = storageFault(
		code.BdevNonRootVFIODisable,
		"VFIO can not be disabled if running as non-root user",
		"Either run server as root or do not disable VFIO when invoking the command")

	// FaultBdevNoIOMMU indicates a missing IOMMU capability.
	FaultBdevNoIOMMU = storageFault(
		code.BdevNoIOMMU,
		"IOMMU capability is required to access NVMe devices but no IOMMU capability detected",
		"enable IOMMU per the DAOS Admin Guide")
)

// FaultPathAccessDenied represents an error where a mount point or device path for
// a storage target is inaccessible because of a permissions issue.
func FaultPathAccessDenied(path string) *fault.Fault {
	return storageFault(
		code.StoragePathAccessDenied,
		fmt.Sprintf("path %q has incompatible access permissions", path),
		"verify the path is accessible by the user running daos_server and try again",
	)
}

// FaultInvalidSPDKConfig creates a Fault for the case where SPDK configuration is invalid.
func FaultInvalidSPDKConfig(err error) *fault.Fault {
	return storageFault(
		code.SpdkInvalidConfiguration,
		fmt.Sprintf("unable to parse SPDK configuration: %s", err),
		"regenerate the configuration and restart daos_server",
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
