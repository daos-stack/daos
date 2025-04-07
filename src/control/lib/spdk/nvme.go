//
// (C) Copyright 2018-2022 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package spdk

import (
	"os"
	"path/filepath"
	"strings"

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

// LockfileAddrCheckFn is a function supplied to the Clean API call which can be used to decide
// whether to remove a lockfile for device or not based on its PCI address. This is necessary so
// that logic outside of this package can be used to determine which addresses to process.
type LockfileAddrCheckFn func(ctrlrPciAddr string) (bool, error)

// Nvme is the interface that provides SPDK NVMe functionality.
type Nvme interface {
	// Discover NVMe controllers and namespaces, and device health info
	Discover(logging.Logger) (storage.NvmeControllers, error)
	// Format NVMe controller namespaces
	Format(logging.Logger) ([]*FormatResult, error)
	// Update updates the firmware on a specific PCI address and slot
	Update(log logging.Logger, ctrlrPciAddr string, path string, slot int32) error
	// Clean removes lockfiles associated with NVMe controllers. Decisions regarding which
	// lockfiles to remove made using supplied address check function.
	Clean(LockfileAddrCheckFn) ([]string, error)
}

// NvmeImpl is an implementation of the Nvme interface.
type NvmeImpl struct{}

// Static base-dir and prefix for SPDK generated lockfiles.
const (
	lockflleDir    = "/var/tmp/"
	lockfilePrefix = "spdk_pci_lock_"
)

type remFunc func(name string) error

// cleanLockfiles removes SPDK lockfiles after binding operations. Takes function which decides
// which of the found lock files to remove based on the address appended to the filename.
func cleanLockfiles(remove remFunc, shouldRemove LockfileAddrCheckFn) ([]string, error) {
	entries, err := os.ReadDir(lockflleDir)
	if err != nil {
		return nil, errors.Wrapf(err, "reading spdk lockfile directory %q", lockflleDir)
	}

	var removed []string
	var outErr error
	for _, v := range entries {
		if v.IsDir() {
			continue
		}
		if !strings.HasPrefix(v.Name(), lockfilePrefix) {
			continue
		}

		lfAddr := strings.Replace(v.Name(), lockfilePrefix, "", 1)
		lfName := filepath.Join(lockfilePrefix, v.Name())

		if ok, err := shouldRemove(lfAddr); err != nil {
			outErr = wrapCleanError(outErr, errors.Wrap(err, lfName))
			continue
		} else if !ok {
			continue
		}

		if err := remove(lfName); err != nil {
			if !os.IsNotExist(err) {
				outErr = wrapCleanError(outErr, errors.Wrap(err, lfName))
			}
			continue
		}
		removed = append(removed, lfName)
	}

	return removed, nil
}

// wrapCleanError encapsulates inErr inside any cleanErr.
func wrapCleanError(inErr error, cleanErr error) (outErr error) {
	outErr = inErr

	if cleanErr != nil {
		if outErr == nil {
			outErr = cleanErr
		}
		outErr = errors.Wrap(inErr, cleanErr.Error())
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
