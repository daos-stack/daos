//
// (C) Copyright 2018-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// Package code is a central repository for all control plane fault codes.
package code

import (
	"encoding/json"
	"strconv"
)

// Code represents a stable fault code.
//
// NB: All control plane errors should register their codes in the
// following block in order to avoid conflicts.
//
// Also note that new codes should always be added at the bottom of
// their respective blocks. This ensures stability of fault codes
// over time.
type Code int

// UnmarshalJSON implements a custom unmarshaler
// to convert an int or string code to a Code.
func (c *Code) UnmarshalJSON(data []byte) (err error) {
	var ic int
	if err = json.Unmarshal(data, &ic); err == nil {
		*c = Code(ic)
		return
	}

	var sc string
	if err = json.Unmarshal(data, &sc); err != nil {
		return
	}

	if ic, err = strconv.Atoi(sc); err == nil {
		*c = Code(ic)
	}
	return
}

// general fault codes
const (
	Unknown Code = iota
	MissingSoftwareDependency
	BadVersionSoftwareDependency
	PrivilegedHelperNotPrivileged
	PrivilegedHelperNotAvailable
	PrivilegedHelperRequestFailed
)

// generic storage fault codes
const (
	StorageUnknown Code = iota + 100
	StorageAlreadyFormatted
	StorageFilesystemAlreadyMounted
	StorageDeviceAlreadyMounted
	StorageTargetAlreadyMounted
	StoragePathAccessDenied
)

// SCM fault codes
const (
	ScmUnknown Code = iota + 200
	ScmFormatInvalidSize
	ScmFormatInvalidDeviceCount
	ScmFormatMissingMountpoint
	ScmFormatMissingDevice
	ScmFormatMissingParam
	ScmFormatConflictingParam
	ScmPathAccessDenied
	ScmDiscoveryFailed
	ScmDuplicatesInDeviceList
	ScmNoDevicesMatchFilter
	ScmNoPMem
	ScmBadRegion
	ScmInvalidPMem
	ScmRamdiskLowMem
	ScmConfigTierMissing
)

// Bdev fault codes
const (
	BdevUnknown Code = iota + 300
	BdevFormatUnknownClass
	BdevFormatFailure
	BdevBadPCIAddress
	BdevNotFound
	BdevDuplicatesInDeviceList
	BdevNoDevicesMatchFilter
	BdevAccelEngineUnknown
	BdevConfigOptFlagUnknown
	BdevConfigTierTypeMismatch
	BdevNonRootVFIODisable
	BdevNoIOMMU
	BdevConfigRolesWithDCPM
	BdevConfigRolesBadNr
	BdevConfigRolesMissing
	BdevConfigMultiTierWithoutRoles
	BdevConfigBadNrTiersWithRoles
	BdevConfigControlMetadataNoRoles
	BdevConfigRolesNoControlMetadata
	BdevConfigRolesWalDataNoMeta
)

// DAOS system fault codes
const (
	SystemUnknown Code = iota + 400
	SystemBadFaultDomainDepth
	SystemPoolLocked
)

// client fault codes
const (
	ClientUnknown Code = iota + 500
	ClientConfigBadControlPort
	ClientConfigBadAccessPoints
	ClientConfigEmptyHostList
	ClientConnectionBadHost
	ClientConnectionNoRoute
	ClientConnectionRefused
	ClientConnectionClosed
	ClientConnectionTimedOut
	ClientFormatRunningSystem
	ClientRpcTimeout
	ClientConfigVMDImbalance
)

// server fault codes
const (
	ServerUnknown Code = iota + 600
	ServerScmUnmanaged
	ServerIommuDisabled
	ServerWrongSystem
	ServerPoolScmTooSmall
	ServerPoolNvmeTooSmall
	ServerPoolInvalidRanks
	ServerPoolInvalidNumRanks
	ServerPoolInvalidServiceReps
	ServerPoolDuplicateLabel
	ServerHarnessNotStarted
	ServerDataPlaneNotStarted
	ServerInstancesNotStopped
	ServerConfigInvalidNetDevClass
	ServerVfioDisabled
	ServerPoolNoLabel
	ServerIncompatibleComponents
	ServerNoCompatibilityInsecure
	ServerPoolHasContainers
)

// server config fault codes
const (
	ServerConfigUnknown Code = iota + 700
	ServerBadConfig
	ServerNoConfigPath
	ServerConfigBadControlPort
	ServerConfigBadTelemetryPort
	ServerConfigBadAccessPoints
	ServerConfigEvenAccessPoints
	ServerConfigBadProvider
	ServerConfigNoEngines
	ServerConfigDuplicateFabric
	ServerConfigDuplicateLogFile
	ServerConfigDuplicateScmMount
	ServerConfigDuplicateScmDeviceList
	ServerConfigOverlappingBdevDeviceList
	ServerConfigBdevCountMismatch
	ServerConfigTargetCountMismatch
	ServerConfigHelperStreamCountMismatch
	ServerConfigFaultDomainInvalid
	ServerConfigFaultCallbackNotFound
	ServerConfigFaultCallbackInsecure
	ServerConfigFaultCallbackBadPerms
	ServerConfigFaultCallbackFailed
	ServerConfigBothFaultPathAndCb
	ServerConfigFaultCallbackEmpty
	ServerConfigFaultDomainTooManyLayers
	ServerConfigNrHugepagesOutOfRange
	ServerConfigHugepagesDisabled
	ServerConfigVMDSettingDuplicate
	ServerConfigEngineNUMAImbalance
	ServerConfigControlMetadataNoPath
	ServerConfigRamdiskUnderMinMem
	ServerConfigRamdiskOverMaxMem
	ServerConfigScmDiffClass
	ServerConfigEngineBdevRolesMismatch
	ServerConfigSysRsvdZero
)

// SPDK library bindings codes
const (
	SpdkUnknown Code = iota + 800
	SpdkCtrlrNoHealth
	SpdkBindingRetNull
	SpdkBindingFailed
)

// security fault codes
const (
	SecurityUnknown Code = iota + 900
	SecurityMissingCertFile
	SecurityUnreadableCertFile
	SecurityInvalidCert
)

const (
	ControlMetadataUnknown Code = iota + 1000
	ControlMetadataBadFilesystem
)
