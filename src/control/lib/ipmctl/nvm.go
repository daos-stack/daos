//
// (C) Copyright 2018-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// Package ipmctl provides Go bindings for libipmctl Native Management API
package ipmctl

// CGO_CFLAGS & CGO_LDFLAGS env vars can be used
// to specify additional dirs.

/*

#include "nvm_management.h"
*/
import "C"
import (
	"github.com/daos-stack/daos/src/control/logging"
)

type (
	// IpmCtl is the interface that provides access to libipmctl.
	IpmCtl interface {
		// Init verifies the version of the library is compatible.
		Init(logging.Logger) error
		// GetModules discovers persistent memory modules.
		GetModules(logging.Logger) ([]DeviceDiscovery, error)
		// GetRegions discovers persistent memory regions.
		GetRegions(logging.Logger) ([]PMemRegion, error)
		// DeleteConfigGoals removes any pending but not yet applied PMem configuration goals.
		DeleteConfigGoals(logging.Logger) error
		// GetFirmwareInfo retrieves firmware information from persistent memory modules.
		GetFirmwareInfo(uid DeviceUID) (DeviceFirmwareInfo, error)
		// UpdateFirmware updates persistent memory module firmware.
		UpdateFirmware(uid DeviceUID, fwPath string, force bool) error
	}

	// NvmMgmt is an implementation of the IpmCtl interface which exercises
	// libipmctl's NVM API.
	NvmMgmt struct {
		initialized bool
	}
)
