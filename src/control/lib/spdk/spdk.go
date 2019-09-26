//
// (C) Copyright 2018-2019 Intel Corporation.
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
#cgo LDFLAGS: -lspdk

#include "stdlib.h"
#include "spdk/stdinc.h"
#include "spdk/env.h"
*/
import "C"

import (
	"errors"
	"fmt"
	"unsafe"
)

// ENV is the interface that provides SPDK environment management.
type ENV interface {
	InitSPDKEnv(EnvOptions) error
}

// Env is a simple ENV implementation.
type Env struct{}

// Rc2err returns an failure if rc != 0.
//
// TODO: If err is already set then it is wrapped,
// otherwise it is ignored. e.g.
// func Rc2err(label string, rc C.int, err error) error {
func Rc2err(label string, rc C.int) error {
	if rc != 0 {
		if rc < 0 {
			rc = -rc
		}
		// e := errors.Error(rc)
		return fmt.Errorf("%s: %d", label, rc) // e
	}
	return nil
}

type EnvOptions struct {
	MemSize      int // memory size in MB for DPDK
	ShmID        int
	PciWhitelist []string
	PciBlacklist []string
}

func convertPciList(in []string, out **C.struct_spdk_pci_addr) error {
	inLen := len(in)
	tmp := (*C.struct_spdk_pci_addr)(C.calloc(C.size_t(inLen), C.sizeof_struct_spdk_pci_addr))
	if tmp == nil {
		return errors.New("failed to alloc memory for pci list")
	}
	tmpSlice := (*[1 << 30]C.struct_spdk_pci_addr)(unsafe.Pointer(tmp))[:inLen:inLen]

	for i, bdf := range in {
		addr := tmpSlice[i]
		rc := C.spdk_pci_addr_parse(&addr, C.CString(bdf))
		if rc < 0 {
			C.free(unsafe.Pointer(tmp))
			return fmt.Errorf("failed to parse %q as pci addr: %s", bdf, C.GoString(C.strerror(rc)))
		}
		tmpSlice[i] = addr
	}

	*out = tmp
	return nil
}

func (o EnvOptions) toC() (*C.struct_spdk_env_opts, error) {
	opts := &C.struct_spdk_env_opts{}

	C.spdk_env_opts_init(opts)

	// ShmID 0 will be ignored
	if o.ShmID > 0 {
		opts.shm_id = C.int(o.ShmID)
	}

	if o.MemSize > 0 {
		opts.mem_size = C.int(o.MemSize)
	}

	if len(o.PciWhitelist) > 0 {
		if err := convertPciList(o.PciWhitelist, &opts.pci_whitelist); err != nil {
			return nil, err
		}
	}

	if len(o.PciBlacklist) > 0 {
		if err := convertPciList(o.PciBlacklist, &opts.pci_blacklist); err != nil {
			return nil, err
		}
	}

	return opts, nil
}

// InitSPDKEnv initializes the SPDK environment.
//
// SPDK relies on an abstraction around the local environment
// named env that handles memory allocation and PCI device operations.
// The library must be initialized first.
func (e *Env) InitSPDKEnv(opts EnvOptions) error {
	cOpts, err := opts.toC()
	if err != nil {
		return err
	}

	rc := C.spdk_env_init(cOpts)
	if err := Rc2err("spdk_env_opts_init", rc); err != nil {
		return err
	}

	if len(opts.PciWhitelist) > 0 {
		C.free(unsafe.Pointer(cOpts.pci_whitelist))
	}
	if len(opts.PciBlacklist) > 0 {
		C.free(unsafe.Pointer(cOpts.pci_blacklist))
	}

	return nil
}
