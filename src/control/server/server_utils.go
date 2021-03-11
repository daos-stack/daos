//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"fmt"
	"io/ioutil"
	"os"
	"path/filepath"

	"github.com/daos-stack/daos/src/control/server/config"
)

const (
	iommuPath        = "/sys/class/iommu"
	minHugePageCount = 128
)

func cfgHasBdevs(cfg *config.Server) bool {
	for _, engineCfg := range cfg.Engines {
		if len(engineCfg.Storage.Bdev.DeviceList) > 0 {
			return true
		}
	}

	return false
}

func iommuDetected() bool {
	// Simple test for now -- if the path exists and contains
	// DMAR entries, we assume that's good enough.
	dmars, err := ioutil.ReadDir(iommuPath)
	if err != nil {
		return false
	}

	return len(dmars) > 0
}

func raftDir(cfg *config.Server) string {
	if len(cfg.Engines) == 0 {
		return "" // can't save to SCM
	}
	return filepath.Join(cfg.Engines[0].Storage.SCM.MountPoint, "control_raft")
}

func hostname() string {
	hn, err := os.Hostname()
	if err != nil {
		return fmt.Sprintf("Hostname() failed: %s", err.Error())
	}
	return hn
}
