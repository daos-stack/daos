//
// (C) Copyright 2018-2021 Intel Corporation.
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

const (
	// general fault codes
	Unknown Code = iota
	MissingSoftwareDependency
	BadVersionSoftwareDependency
	PrivilegedHelperNotPrivileged
	PrivilegedHelperNotAvailable
	PrivilegedHelperRequestFailed

	// generic storage fault codes
	StorageUnknown Code = iota + 100
	StorageAlreadyFormatted
	StorageFilesystemAlreadyMounted
	StorageDeviceAlreadyMounted
	StorageTargetAlreadyMounted

	// SCM fault codes
	ScmUnknown Code = iota + 200
	ScmFormatInvalidSize
	ScmFormatInvalidDeviceCount
	ScmFormatMissingMountpoint
	ScmFormatMissingDevice
	ScmFormatMissingParam
	ScmFormatConflictingParam
	ScmDiscoveryFailed
	ScmDuplicatesInDeviceList
	ScmNoDevicesMatchFilter

	// Bdev fault codes
	BdevUnknown Code = iota + 300
	BdevFormatUnknownClass
	BdevFormatFailure
	BdevBadPCIAddress
	BdevPCIAddressNotFound
	BdevDuplicatesInDeviceList
	BdevNoDevicesMatchFilter

	// DAOS system fault codes
	SystemUnknown Code = iota + 400

	// client fault codes
	ClientUnknown Code = iota + 500
	ClientConfigBadControlPort
	ClientConfigBadAccessPoints
	ClientConfigEmptyHostList
	ClientConnectionBadHost
	ClientConnectionNoRoute
	ClientConnectionRefused
	ClientConnectionClosed

	// server fault codes
	ServerUnknown Code = iota + 600
	ServerScmUnmanaged
	ServerBdevNotFound
	ServerIommuDisabled
	ServerWrongSystem
	ServerPoolScmTooSmall
	ServerPoolNvmeTooSmall
	ServerPoolInvalidRanks
	ServerPoolInvalidServiceReps
	ServerPoolDuplicateLabel
	ServerInsufficientFreeHugePages
	ServerHarnessNotStarted
	ServerDataPlaneNotStarted
	ServerInstancesNotStopped
	ServerConfigInvalidNetDevClass
	ServerVfioDisabled

	// server config fault codes
	ServerConfigUnknown Code = iota + 700
	ServerBadConfig
	ServerNoConfigPath
	ServerConfigBadControlPort
	ServerConfigBadAccessPoints
	ServerConfigEvenAccessPoints
	ServerConfigBadProvider
	ServerConfigNoServers
	ServerConfigDuplicateFabric
	ServerConfigDuplicateLogFile
	ServerConfigDuplicateScmMount
	ServerConfigDuplicateScmDeviceList
	ServerConfigOverlappingBdevDeviceList
	ServerConfigFaultDomainInvalid
	ServerConfigFaultCallbackNotFound
	ServerConfigFaultCallbackBadPerms
	ServerConfigFaultCallbackFailed
	ServerConfigBothFaultPathAndCb
	ServerConfigFaultCallbackEmpty

	// SPDK library bindings codes
	SpdkUnknown Code = iota + 800
	SpdkCtrlrNoHealth
	SpdkBindingRetNull
	SpdkBindingFailed

	// security fault codes
	SecurityUnknown Code = iota + 900
)
