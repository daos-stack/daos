//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build !spdk
// +build !spdk

package spdk

import (
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// Discover NVMe devices.
func (n *NvmeImpl) Discover(log logging.Logger) (storage.NvmeControllers, error) {
	return storage.NvmeControllers{}, nil
}

// Format devices available through SPDK.
func (n *NvmeImpl) Format(log logging.Logger) ([]*FormatResult, error) {
	return []*FormatResult{}, nil
}

// Update updates the firmware image via SPDK in a given slot on the device.
func (n *NvmeImpl) Update(log logging.Logger, ctrlrPciAddr string, path string, slot int32) error {
	return nil
}
