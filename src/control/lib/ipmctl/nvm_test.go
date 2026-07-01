//
// (C) Copyright 2018-2022 Intel Corporation.
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build linux && amd64
// +build linux,amd64

package ipmctl

import (
	"fmt"
	"os/user"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

// NVM API calls will fail if not run as root. We should just skip the tests.
func skipNoPerms(t *testing.T) bool {
	t.Helper()
	u, err := user.Current()
	if err != nil {
		t.Fatalf("can't determine current user: %v", err)
	}
	if u.Uid != "0" {
		// Alert the user even if they're not running the tests in verbose mode
		fmt.Printf("%s must be run as root\n", t.Name())
		return true
	}
	return false
}

// Fetch all devices in the system - and skip the test if there are none
func discoverDevices(t *testing.T, log logging.Logger, mgmt NvmMgmt) []DeviceDiscovery {
	t.Helper()

	devs, err := mgmt.GetModules(log)
	if err != nil {
		t.Fatalf("Discovery failed: %s", err.Error())
	}

	if len(devs) == 0 {
		t.Skip("no NVM devices on system")
	}

	return devs
}

func TestNvmDiscovery(t *testing.T) {
	log, buf := logging.NewTestLogger("discovery")
	defer test.ShowBufferOnFailure(t, buf)

	if skipNoPerms(t) {
		return
	}

	mgmt := NvmMgmt{}
	_, err := mgmt.GetModules(log)
	if err != nil {
		t.Fatalf("Discovery failed: %s", err.Error())
	}
}

// The actual test functions are in nvm_ctest.go file so that they can use cgo (import "C").
// These wrappers are here for gotest to find.

func TestGetModules(t *testing.T) {
	testGetModules(t)
}

func TestGetRegions(t *testing.T) {
	testGetRegions(t)
}
