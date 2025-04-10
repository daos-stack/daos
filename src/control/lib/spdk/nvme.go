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

// Static prefix for SPDK generated lockfiles.
const LockfilePrefix = "spdk_pci_lock_"

type removeFn func(name string) error

// cleanLockfiles removes SPDK lockfiles after binding operations. Takes function which decides
// which of the found lock files to remove based on the address appended to the filename.
func cleanLockfiles(dir string, pciAddrChecker LockfileAddrCheckFn, remove removeFn) ([]string, error) {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return nil, errors.Wrapf(err, "reading spdk lockfile directory %q", dir)
	}

	var removed []string
	var outErr error
	for _, v := range entries {
		if v.IsDir() {
			continue
		}
		if !strings.HasPrefix(v.Name(), LockfilePrefix) {
			continue
		}

		lfAddr := strings.Replace(v.Name(), LockfilePrefix, "", 1)
		lfName := filepath.Join(dir, v.Name())

		if shouldRemove, err := pciAddrChecker(lfAddr); err != nil {
			outErr = wrapCleanError(outErr, errors.Wrap(err, lfName))
			continue
		} else if !shouldRemove {
			continue
		}

		if err := remove(lfName); err != nil {
			// In case lockfile is removed between time of read-dir and remove.
			if os.IsNotExist(err) {
				continue
			}
			outErr = wrapCleanError(outErr, errors.Wrap(err, lfName))
		}
		removed = append(removed, lfName)
	}

	return removed, outErr
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
