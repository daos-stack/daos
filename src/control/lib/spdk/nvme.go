//
// (C) Copyright 2018-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package spdk

import (
	"os"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// FormatResult struct mirrors C.struct_wipe_res_t and describes the results of a format operation
// on an NVMe controller namespace.
type FormatResult struct {
	CtrlrPCIAddr string
	NsID         uint32
	Err          error
}

// Nvme is the interface that provides SPDK NVMe functionality.
type Nvme interface {
	// Discover NVMe controllers and namespaces, and device health info
	Discover(logging.Logger) (storage.NvmeControllers, error)
	// Format NVMe controller namespaces
	Format(logging.Logger) ([]*FormatResult, error)
	// Update updates the firmware on a specific PCI address and slot
	Update(log logging.Logger, ctrlrPciAddr string, path string, slot int32) error
}

// NvmeImpl is an implementation of the Nvme interface.
type NvmeImpl struct{}

const lockfilePathPrefix = "/var/tmp/spdk_pci_lock_"

type remFunc func(name string) error

// cleanLockfiles removes SPDK lockfiles after binding operations.
func cleanLockfiles(log logging.Logger, remove remFunc, pciAddrs ...string) error {
	pciAddrs = common.DedupeStringSlice(pciAddrs)
	removed := make([]string, 0, len(pciAddrs))

	for _, pciAddr := range pciAddrs {
		fName := lockfilePathPrefix + pciAddr

		if err := remove(fName); err != nil {
			if os.IsNotExist(err) {
				continue
			}
			return errors.Wrapf(err, "remove %s", fName)
		}
		removed = append(removed, fName)
	}
	log.Debugf("removed lockfiles: %v", removed)

	return nil
}

// wrapCleanError encapsulates inErr inside any cleanErr.
func wrapCleanError(inErr error, cleanErr error) (outErr error) {
	outErr = inErr

	if cleanErr != nil {
		outErr = errors.Wrap(inErr, cleanErr.Error())
		if outErr == nil {
			outErr = cleanErr
		}
	}

	return
}

func resultPCIAddresses(results []*FormatResult) []string {
	pciAddrs := make([]string, 0, len(results))
	for _, r := range results {
		pciAddrs = append(pciAddrs, r.CtrlrPCIAddr)
	}

	return common.DedupeStringSlice(pciAddrs)
}
