//
// (C) Copyright 2018-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package spdk

// CGO_CFLAGS & CGO_LDFLAGS env vars can be used
// to specify additional dirs.

/*
#cgo CFLAGS: -I .
#cgo LDFLAGS: -L . -lnvme_control
#cgo LDFLAGS: -lspdk_env_dpdk -lspdk_nvme -lspdk_vmd -lspdk_util
#cgo LDFLAGS: -lrte_mempool -lrte_mempool_ring -lrte_bus_pci

#include "stdlib.h"
#include "daos_srv/control.h"
#include "spdk/stdinc.h"
#include "spdk/string.h"
#include "spdk/env.h"
#include "spdk/nvme.h"
#include "include/nvme_control.h"
*/
import "C"

import (
	"os"
	"strings"
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const lockfilePathPrefix = "/var/tmp/spdk_pci_lock_"

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

func ctrlrPCIAddresses(ctrlrs storage.NvmeControllers) []string {
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
// containing any Namespace and DeviceHealth structs.
// Afterwards remove lockfile for each discovered device.
func (n *NvmeImpl) Discover(log logging.Logger) (storage.NvmeControllers, error) {
	if n == nil {
		return nil, errors.New("nil NvmeImpl")
	}

	retPtr := C.nvme_discover()
	ctrlrs, err := collectCtrlrs(retPtr, "NVMe Discover(): C.nvme_discover")
	C.nvme_clean_ret(retPtr)
	C.nvme_clean_globals()

	pciAddrs := ctrlrPCIAddresses(ctrlrs)
	log.Debugf("discovered nvme ssds: %v", pciAddrs)

	return ctrlrs, wrapCleanError(err, cleanLockfiles(log, realRemove, pciAddrs...))
}

func resultPCIAddresses(results []*FormatResult) []string {
	pciAddrs := make([]string, 0, len(results))
	for _, r := range results {
		pciAddrs = append(pciAddrs, r.CtrlrPCIAddr)
	}

	return common.DedupeStringSlice(pciAddrs)
}

// Format devices available through SPDK, destructive operation!
//
// Attempt wipe of each controller namespace's LBA-0.
// Afterwards remove lockfile for each formatted device.
func (n *NvmeImpl) Format(log logging.Logger) ([]*FormatResult, error) {
	if n == nil {
		return nil, errors.New("nil NvmeImpl")
	}

	retPtr := C.nvme_wipe_namespaces()
	results, err := collectFormatResults(retPtr, "NVMe Format(): C.nvme_wipe_namespaces()")
	C.nvme_clean_ret(retPtr)
	C.nvme_clean_globals()

	pciAddrs := resultPCIAddresses(results)
	log.Debugf("formatted nvme ssds: %v", pciAddrs)

	return results, wrapCleanError(err, cleanLockfiles(log, realRemove, pciAddrs...))
}

// Update updates the firmware image via SPDK in a given slot on the device.
//
// Afterwards remove lockfile for the updated device.
func (n *NvmeImpl) Update(log logging.Logger, ctrlrPciAddr string, path string, slot int32) error {
	if n == nil {
		return errors.New("nil NvmeImpl")
	}

	csPath := C.CString(path)
	defer C.free(unsafe.Pointer(csPath))

	csPci := C.CString(ctrlrPciAddr)
	defer C.free(unsafe.Pointer(csPci))

	_, err := collectCtrlrs(C.nvme_fwupdate(csPci, csPath, C.uint(slot)),
		"NVMe Update(): C.nvme_fwupdate")

	return wrapCleanError(err, cleanLockfiles(log, realRemove, ctrlrPciAddr))
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
func c2GoDeviceHealth(hs *C.struct_nvme_stats) *storage.NvmeHealth {
	return &storage.NvmeHealth{
		TempWarnTime:            uint32(hs.warn_temp_time),
		TempCritTime:            uint32(hs.crit_temp_time),
		CtrlBusyTime:            uint64(hs.ctrl_busy_time),
		PowerCycles:             uint64(hs.power_cycles),
		PowerOnHours:            uint64(hs.power_on_hours),
		UnsafeShutdowns:         uint64(hs.unsafe_shutdowns),
		MediaErrors:             uint64(hs.media_errs),
		ErrorLogEntries:         uint64(hs.err_log_entries),
		Temperature:             uint32(hs.temperature),
		TempWarn:                bool(hs.temp_warn),
		AvailSpareWarn:          bool(hs.avail_spare_warn),
		ReliabilityWarn:         bool(hs.dev_reliability_warn),
		ReadOnlyWarn:            bool(hs.read_only_warn),
		VolatileWarn:            bool(hs.volatile_mem_warn),
		ProgFailCntNorm:         uint8(hs.program_fail_cnt_norm),
		ProgFailCntRaw:          uint64(hs.program_fail_cnt_raw),
		EraseFailCntNorm:        uint8(hs.erase_fail_cnt_norm),
		EraseFailCntRaw:         uint64(hs.erase_fail_cnt_raw),
		WearLevelingCntNorm:     uint8(hs.wear_leveling_cnt_norm),
		WearLevelingCntMin:      uint16(hs.wear_leveling_cnt_min),
		WearLevelingCntMax:      uint16(hs.wear_leveling_cnt_max),
		WearLevelingCntAvg:      uint16(hs.wear_leveling_cnt_avg),
		EndtoendErrCntRaw:       uint64(hs.endtoend_err_cnt_raw),
		CrcErrCntRaw:            uint64(hs.crc_err_cnt_raw),
		MediaWearRaw:            uint64(hs.media_wear_raw),
		HostReadsRaw:            uint64(hs.host_reads_raw),
		WorkloadTimerRaw:        uint64(hs.workload_timer_raw),
		ThermalThrottleStatus:   uint8(hs.thermal_throttle_status),
		ThermalThrottleEventCnt: uint64(hs.thermal_throttle_event_cnt),
		RetryBufferOverflowCnt:  uint64(hs.retry_buffer_overflow_cnt),
		PllLockLossCnt:          uint64(hs.pll_lock_loss_cnt),
		NandBytesWritten:        uint64(hs.nand_bytes_written),
		HostBytesWritten:        uint64(hs.host_bytes_written),
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
		err = rc2err(C.GoString(&fmtResult.info[0]), fmtResult.rc)
	}

	return &FormatResult{
		CtrlrPCIAddr: C.GoString(&fmtResult.ctrlr_pci_addr[0]),
		NsID:         uint32(fmtResult.ns_id),
		Err:          err,
	}
}

// checkRet returns fault if ret_t struct input is nil or rc is non-zero.
func checkRet(retPtr *C.struct_ret_t, msgFail string) error {
	if retPtr == nil {
		return FaultBindingRetNull(msgFail)
	}

	if retPtr.rc != 0 {
		var msgs []string

		if msgFail != "" {
			msgs = append(msgs, msgFail)
		}

		msgInfo := C.GoString(&retPtr.info[0])
		if msgInfo != "" {
			msgs = append(msgs, msgInfo)
		}

		msgErrno := C.GoString(C.spdk_strerror(-retPtr.rc))
		if msgErrno != "" {
			msgs = append(msgs, msgErrno)
		}

		return FaultBindingFailed(int(retPtr.rc), strings.Join(msgs, ": "))
	}

	return nil
}

// collectCtrlrs parses return struct to collect slice of nvme.Controller.
func collectCtrlrs(retPtr *C.struct_ret_t, msgFail string) (storage.NvmeControllers, error) {
	if err := checkRet(retPtr, msgFail); err != nil {
		return nil, err
	}

	var ctrlrs storage.NvmeControllers
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
			return ctrlrs, errors.Wrapf(FaultCtrlrNoHealth, "ctrlr %s", ctrlr.PciAddr)
		}
		ctrlr.HealthStats = c2GoDeviceHealth(healthPtr)

		ctrlrs = append(ctrlrs, ctrlr)

		ctrlrPtr = ctrlrPtr.next
	}

	return ctrlrs, nil
}

// collectFormatResults parses return struct to collect slice of
// nvme.FormatResult.
func collectFormatResults(retPtr *C.struct_ret_t, msgFail string) ([]*FormatResult, error) {
	if err := checkRet(retPtr, msgFail); err != nil {
		return nil, err
	}

	var fmtResults []*FormatResult
	fmtResult := retPtr.wipe_results
	for fmtResult != nil {
		fmtResults = append(fmtResults, c2GoFormatResult(fmtResult))
		fmtResult = fmtResult.next
	}

	return fmtResults, nil
}
