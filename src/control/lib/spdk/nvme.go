//
// (C) Copyright 2018-2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package spdk

// CGO_CFLAGS & CGO_LDFLAGS env vars can be used
// to specify additional dirs.

/*
#cgo CFLAGS: -I .
#cgo LDFLAGS: -L . -lnvme_control -lspdk

#include "stdlib.h"
#include "daos_srv/control.h"
#include "spdk/stdinc.h"
#include "spdk/nvme.h"
#include "spdk/env.h"
#include "include/nvme_control.h"
#include "include/nvme_control_common.h"
*/
import "C"

import (
	"os"
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const lockfilePathPrefix = "/tmp/spdk_pci_lock_"

// Nvme is the interface that provides SPDK NVMe functionality.
type Nvme interface {
	// Discover NVMe controllers and namespaces, and device health info
	Discover(logging.Logger) (storage.NvmeControllers, error)
	// Format NVMe controller namespaces
	Format(logging.Logger) ([]*FormatResult, error)
	// CleanLockfiles removes SPDK lockfiles for specific PCI addresses
	CleanLockfiles(logging.Logger, ...string) error
	// Update updates the firmware on a specific PCI address and slot
	Update(log logging.Logger, ctrlrPciAddr string, path string, slot int32) error
}

// NvmeImpl is an implementation of the Nvme interface.
type NvmeImpl struct{}

// FormatResult struct mirrors C.struct_wipe_res_t
// and describes the results of a format operation
// on an NVMe controller namespace.
type FormatResult struct {
	CtrlrPCIAddr string
	NsID         uint32
	Err          error
}

type remFunc func(name string) error

func realRemove(name string) error {
	return os.Remove(name)
}

func cleanLockfiles(log logging.Logger, remove remFunc, pciAddrs ...string) error {
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

// CleanLockfiles removes SPDK lockfiles after binding operations.
func (n *NvmeImpl) CleanLockfiles(log logging.Logger, pciAddrs ...string) error {
	return cleanLockfiles(log, realRemove, pciAddrs...)
}

func pciAddressList(ctrlrs storage.NvmeControllers) []string {
	pciAddrs := make([]string, 0, len(ctrlrs))
	for _, c := range ctrlrs {
		pciAddrs = append(pciAddrs, c.PciAddr)
	}

	return pciAddrs
}

// Discover NVMe devices, including NVMe devices behind VMDs if enabled,
// accessible by SPDK on a given host.
//
// Calls C.nvme_discover which returns pointers to single linked list of
// ctrlr_t structs. These are converted and returned as Controller slices
// containing any Namespace and DeviceHealth structs. Afterwards remove
// lockfile for each discovered device.
func (n *NvmeImpl) Discover(log logging.Logger) (storage.NvmeControllers, error) {
	ctrlrs, err := collectCtrlrs(C.nvme_discover(), "NVMe Discover(): C.nvme_discover")

	pciAddrs := pciAddressList(ctrlrs)
	log.Debugf("discovered nvme ssds: %v", pciAddrs)

	return ctrlrs, wrapCleanError(err, n.CleanLockfiles(log, pciAddrs...))
}

// Format devices available through SPDK, destructive operation!
//
// Attempt wipe of namespace #1 LBA-0.
//
// TODO DAOS-5485: reinstate fall back to full controller format if quick
//      format failed, this requires reworking C.nvme_format() to format
//      all available ctrlrs
func (n *NvmeImpl) Format(log logging.Logger) ([]*FormatResult, error) {
	return collectFormatResults(C.nvme_wipe_namespaces(),
		"NVMe Format(): C.nvme_wipe_namespaces()")

	//	log.Infof("falling back to full format on %s\n", ctrlrPciAddr)
	//	_, err = collectCtrlrs(C.nvme_format(csPci), failMsg+"format()")
	//
	//	return
}

// Update updates the firmware image via SPDK in a given slot on the device.
func (n *NvmeImpl) Update(log logging.Logger, ctrlrPciAddr string, path string, slot int32) error {
	csPath := C.CString(path)
	defer C.free(unsafe.Pointer(csPath))

	csPci := C.CString(ctrlrPciAddr)
	defer C.free(unsafe.Pointer(csPci))

	_, err := collectCtrlrs(C.nvme_fwupdate(csPci, csPath, C.uint(slot)),
		"NVMe Update(): C.nvme_fwupdate")

	return wrapCleanError(err, n.CleanLockfiles(log, ctrlrPciAddr))
}

// c2GoController is a private translation function.
func c2GoController(ctrlr *C.struct_ctrlr_t) *storage.NvmeController {
	return &storage.NvmeController{
		Model:    C.GoString(&ctrlr.model[0]),
		Serial:   C.GoString(&ctrlr.serial[0]),
		PciAddr:  C.GoString(&ctrlr.pci_addr[0]),
		FwRev:    C.GoString(&ctrlr.fw_rev[0]),
		SocketID: int32(ctrlr.socket_id),
	}
}

// c2GoDeviceHealth is a private translation function.
func c2GoDeviceHealth(health *C.struct_nvme_health_stats) *storage.NvmeControllerHealth {
	return &storage.NvmeControllerHealth{
		ErrorCount:      uint64(health.err_count),
		TempWarnTime:    uint32(health.warn_temp_time),
		TempCritTime:    uint32(health.crit_temp_time),
		CtrlBusyTime:    uint64(health.ctrl_busy_time),
		PowerCycles:     uint64(health.power_cycles),
		PowerOnHours:    uint64(health.power_on_hours),
		UnsafeShutdowns: uint64(health.unsafe_shutdowns),
		MediaErrors:     uint64(health.media_errs),
		ErrorLogEntries: uint64(health.err_log_entries),
		ChecksumErrors:  uint32(health.checksum_errs),
		Temperature:     uint32(health.temperature),
		TempWarn:        bool(health.temp_warn),
		AvailSpareWarn:  bool(health.avail_spare_warn),
		ReliabilityWarn: bool(health.dev_reliability_warn),
		ReadOnlyWarn:    bool(health.read_only_warn),
		VolatileWarn:    bool(health.volatile_mem_warn),
	}
}

// c2GoNamespace is a private translation function.
func c2GoNamespace(ns *C.struct_ns_t) *storage.NvmeNamespace {
	return &storage.NvmeNamespace{
		ID:   uint32(ns.id),
		Size: uint64(ns.size),
	}
}

// c2GoFormatResult is a private translation function.
func c2GoFormatResult(fmtResult *C.struct_wipe_res_t) *FormatResult {
	var err error
	if fmtResult.rc != 0 {
		err = Rc2err(C.GoString(&fmtResult.info[0]), fmtResult.rc)
	}

	return &FormatResult{
		CtrlrPCIAddr: C.GoString(&fmtResult.ctrlr_pci_addr[0]),
		NsID:         uint32(fmtResult.ns_id),
		Err:          err,
	}
}

// clean deallocates memory in return structure and frees the pointer.
func clean(retPtr *C.struct_ret_t) {
	C.clean_ret(retPtr)
	C.free(unsafe.Pointer(retPtr))
}

// collectCtrlrs parses return struct to collect slice of nvme.Controller.
func collectCtrlrs(retPtr *C.struct_ret_t, failMsg string) (ctrlrs storage.NvmeControllers, err error) {
	if retPtr == nil {
		return nil, errors.Wrap(FaultBindingRetNull, failMsg)
	}

	defer clean(retPtr)

	if retPtr.rc != 0 {
		err = errors.Wrap(FaultBindingFailed(int(retPtr.rc),
			C.GoString(&retPtr.info[0])), failMsg)

		return
	}

	ctrlrPtr := retPtr.ctrlrs
	for ctrlrPtr != nil {
		ctrlr := c2GoController(ctrlrPtr)

		if nsPtr := ctrlrPtr.nss; nsPtr != nil {
			for nsPtr != nil {
				ctrlr.Namespaces = append(ctrlr.Namespaces,
					c2GoNamespace(nsPtr))
				nsPtr = nsPtr.next
			}
		}

		healthPtr := ctrlrPtr.stats
		if healthPtr == nil {
			err = FaultCtrlrNoHealth

			return
		}
		ctrlr.HealthStats = c2GoDeviceHealth(healthPtr)

		ctrlrs = append(ctrlrs, ctrlr)

		ctrlrPtr = ctrlrPtr.next
	}

	return
}

// collectFormatResults parses return struct to collect slice of
// nvme.FormatResult.
func collectFormatResults(retPtr *C.struct_ret_t, failMsg string) ([]*FormatResult, error) {
	if retPtr == nil {
		return nil, errors.Wrap(FaultBindingRetNull, failMsg)
	}

	defer clean(retPtr)

	if retPtr.rc != 0 {
		return nil, errors.Wrap(FaultBindingFailed(int(retPtr.rc),
			C.GoString(&retPtr.info[0])), failMsg)
	}

	var fmtResults []*FormatResult
	fmtResult := retPtr.wipe_results
	for fmtResult != nil {
		fmtResults = append(fmtResults, c2GoFormatResult(fmtResult))
		fmtResult = fmtResult.next
	}

	return fmtResults, nil
}
