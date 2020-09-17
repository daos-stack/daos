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

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/pkg/errors"
)

// Env is the interface that provides SPDK environment management.
type Env interface {
	InitSPDKEnv(logging.Logger, EnvOptions) error
	FiniSPDKEnv(logging.Logger, EnvOptions)
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
	MemSize      int      // size in MiB to be allocated to SPDK proc
	PciWhiteList []string // restrict SPDK device access
	DisableVMD   bool     // flag if VMD devices should not be included
}

func (o EnvOptions) toC() (opts *C.struct_spdk_env_opts, cWhiteListPtr *unsafe.Pointer, err error) {
	opts = new(C.struct_spdk_env_opts)

	C.spdk_env_opts_init(opts)

	if o.MemSize > 0 {
		opts.mem_size = C.int(o.MemSize)
	}

	// quiet DPDK EAL logging by setting log level to ERROR
	opts.env_context = unsafe.Pointer(C.CString("--log-level=lib.eal:4"))

	if len(o.PciWhiteList) > 0 {
		cWhiteListPtr, err = pciListToC(o.PciWhiteList)
		if err != nil {
			return
		}
		opts.pci_whitelist = (*C.struct_spdk_pci_addr)(*cWhiteListPtr)
		opts.num_pci_addr = C.ulong(len(o.PciWhiteList))
	}

	return
}

func pciListToC(inAddrs []string) (*unsafe.Pointer, error) {
	var tmpAddr *C.struct_spdk_pci_addr
	structSize := unsafe.Sizeof(*tmpAddr)

	outAddrs := C.calloc(C.ulong(len(inAddrs)), C.ulong(structSize))

	for i, inAddr := range inAddrs {
		offset := uintptr(i) * structSize
		tmpAddr = (*C.struct_spdk_pci_addr)(unsafe.Pointer(uintptr(outAddrs) + offset))

		if rc := C.spdk_pci_addr_parse(tmpAddr, C.CString(inAddr)); rc != 0 {
			C.free(unsafe.Pointer(outAddrs))
			return nil, Rc2err("spdk_pci_addr_parse()", rc)
		}
	}

	return &outAddrs, nil
}

// InitSPDKEnv initializes the SPDK environment.
//
// SPDK relies on an abstraction around the local environment
// named env that handles memory allocation and PCI device operations.
// The library must be initialized first.
func (e *EnvImpl) InitSPDKEnv(log logging.Logger, opts EnvOptions) error {
	log.Debugf("spdk init go opts: %+v", opts)

	cOpts, toFree, err := opts.toC()
	if err != nil {
		return errors.Wrap(err, "convert spdk env opts to C")
	}
	if toFree != nil {
		defer C.free(unsafe.Pointer(*toFree))
	}

	if rc := C.spdk_env_init(cOpts); rc != 0 {
		return Rc2err("spdk_env_init()", rc)
	}

	log.Debugf("spdk init c opts: %+v", cOpts)

	if opts.DisableVMD {
		return nil
	}

	if rc := C.spdk_vmd_init(); rc != 0 {
		return Rc2err("spdk_vmd_init()", rc)
	}

	return nil
}

// FiniSPDKEnv initializes the SPDK environment.
func (e *EnvImpl) FiniSPDKEnv(log logging.Logger, opts EnvOptions) {
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
