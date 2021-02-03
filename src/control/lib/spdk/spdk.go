//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// Package spdk provides Go bindings for SPDK
package spdk

// CGO_CFLAGS & CGO_LDFLAGS env vars can be used
// to specify additional dirs.

/*
#cgo LDFLAGS: -lspdk_env_dpdk -lspdk_vmd  -lrte_mempool -lrte_mempool_ring -lrte_bus_pci
#cgo LDFLAGS: -lrte_pci -lrte_ring -lrte_mbuf -lrte_eal -lrte_kvargs -ldl -lnuma

#include <stdlib.h>
#include <spdk/stdinc.h>
#include <spdk/string.h>
#include <spdk/env.h>
#include <spdk/vmd.h>
*/
import "C"

import (
	"fmt"
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

// Env is the interface that provides SPDK environment management.
type Env interface {
	InitSPDKEnv(logging.Logger, *EnvOptions) error
	FiniSPDKEnv(logging.Logger, *EnvOptions)
}

// EnvImpl is a an implementation of the Env interface.
type EnvImpl struct{}

// Rc2err returns error from label and rc.
func Rc2err(label string, rc C.int) error {
	return fmt.Errorf("%s: %d", label, rc)
}

// EnvOptions describe parameters to be used when initializing a processes
// SPDK environment.
type EnvOptions struct {
	MemSize        int      // size in MiB to be allocated to SPDK proc
	PciIncludeList []string // restrict SPDK device access
	DisableVMD     bool     // flag if VMD devices should not be included
}

func (o *EnvOptions) toC(log logging.Logger) (*C.struct_spdk_env_opts, func(), error) {
	opts := new(C.struct_spdk_env_opts)

	C.spdk_env_opts_init(opts)

	if o.MemSize > 0 {
		opts.mem_size = C.int(o.MemSize)
	}

	// quiet DPDK EAL logging by setting log level to ERROR
	opts.env_context = unsafe.Pointer(C.CString("--log-level=lib.eal:4"))

	if len(o.PciIncludeList) > 0 {
		cPtr, err := pciListToC(log, o.PciIncludeList)
		if err != nil {
			return nil, nil, err
		}

		opts.pci_whitelist = (*C.struct_spdk_pci_addr)(*cPtr)
		opts.num_pci_addr = C.ulong(len(o.PciIncludeList))

		closure := func() {
			if cPtr != nil {
				C.free(unsafe.Pointer(*cPtr))
			}
		}

		return opts, closure, nil
	}

	return opts, func() {}, nil
}

func (o *EnvOptions) sanitizeIncludeList(log logging.Logger) error {
	if !o.DisableVMD {
		// DPDK will not accept VMD backing device addresses
		// so convert to VMD address
		newIncludeList, err := revertBackingToVmd(log, o.PciIncludeList)
		if err != nil {
			return err
		}
		o.PciIncludeList = newIncludeList
	}

	return nil
}

// pciListToC allocates memory and populate array of C SPDK PCI addresses.
func pciListToC(log logging.Logger, inAddrs []string) (*unsafe.Pointer, error) {
	var tmpAddr *C.struct_spdk_pci_addr

	structSize := unsafe.Sizeof(*tmpAddr)
	outAddrs := C.calloc(C.ulong(len(inAddrs)), C.ulong(structSize))

	for i, inAddr := range inAddrs {
		log.Debugf("adding %s to spdk_env_opts include list", inAddr)

		offset := uintptr(i) * structSize
		tmpAddr = (*C.struct_spdk_pci_addr)(unsafe.Pointer(uintptr(outAddrs) + offset))

		if rc := C.spdk_pci_addr_parse(tmpAddr, C.CString(inAddr)); rc != 0 {
			C.free(unsafe.Pointer(outAddrs))

			return nil, Rc2err("spdk_pci_addr_parse()", rc)
		}
	}

	return &outAddrs, nil
}

// revertBackingToVmd converts VMD backing device PCI addresses (with the VMD
// address encoded in the domain component of the PCI address) back to the PCI
// address of the VMD e.g. [5d0505:01:00.0, 5d0505:03:00.0] -> [0000:5d:05.5].
//
// Many assumptions are made as to the input and output PCI address structure in
// the conversion.
func revertBackingToVmd(log logging.Logger, pciAddrs []string) ([]string, error) {
	var outAddrs []string

	for _, inAddr := range pciAddrs {
		domain, _, _, _, err := common.ParsePCIAddress(inAddr)
		if err != nil {
			return nil, err
		}
		if domain == 0 {
			outAddrs = append(outAddrs, inAddr)
			continue
		}

		domainStr := fmt.Sprintf("%x", domain)
		if len(domainStr) != 6 {
			return nil, errors.New("unexpected length of domain")
		}

		outAddr := fmt.Sprintf("0000:%s:%s.%s",
			domainStr[:2], domainStr[2:4], domainStr[5:])
		if !common.Includes(outAddrs, outAddr) {
			log.Debugf("replacing backing device %s with vmd %s", inAddr, outAddr)
			outAddrs = append(outAddrs, outAddr)
		}
	}

	return outAddrs, nil
}

// InitSPDKEnv initializes the SPDK environment.
//
// SPDK relies on an abstraction around the local environment
// named env that handles memory allocation and PCI device operations.
// The library must be initialized first.
func (e *EnvImpl) InitSPDKEnv(log logging.Logger, opts *EnvOptions) error {
	log.Debugf("spdk init go opts: %+v", opts)

	if err := opts.sanitizeIncludeList(log); err != nil {
		return errors.Wrap(err, "sanitizing PCI include list")
	}

	cOpts, freeMem, err := opts.toC(log)
	if err != nil {
		return errors.Wrap(err, "convert spdk env opts to C")
	}
	defer freeMem()
	log.Debugf("spdk init c opts: %+v", cOpts)

	if rc := C.spdk_env_init(cOpts); rc != 0 {
		return Rc2err("spdk_env_init()", rc)
	}

	if opts.DisableVMD {
		return nil
	}

	if rc := C.spdk_vmd_init(); rc != 0 {
		return Rc2err("spdk_vmd_init()", rc)
	}

	return nil
}

// FiniSPDKEnv initializes the SPDK environment.
func (e *EnvImpl) FiniSPDKEnv(log logging.Logger, opts *EnvOptions) {
	log.Debugf("spdk fini go opts: %+v", opts)

	C.spdk_env_fini()

	// TODO: enable when vmd_fini supported in daos spdk version
	//	if opts.DisableVMD {
	//		return nil
	//	}
	//
	//	if rc := C.spdk_vmd_fini(); rc != 0 {
	//		return Rc2err("spdk_vmd_fini()", rc)
	//	}
}
