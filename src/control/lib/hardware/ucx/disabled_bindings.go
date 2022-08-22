//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build !ucx
// +build !ucx

package ucx

import (
	"github.com/daos-stack/daos/src/control/lib/dlopen"
	"github.com/daos-stack/daos/src/control/lib/hardware"
)

var errNotSupported = hardware.ErrUnsupportedFabric("ucx")

// Load reports that the library is not supported.
func Load() (func(), error) {
	return nil, errNotSupported
}

func openUCT() (*dlopen.LibHandle, error) {
	return nil, errNotSupported
}

type uctComponent struct {
	name string
}

func getUCTComponents(uctHdl *dlopen.LibHandle) ([]*uctComponent, func() error, error) {
	return nil, nil, errNotSupported
}

func getMDResourceNames(uctHdl *dlopen.LibHandle, component *uctComponent) ([]string, error) {
	return nil, errNotSupported
}

type uctMDConfig struct {
}

func getComponentMDConfig(uctHdl *dlopen.LibHandle, comp *uctComponent) (*uctMDConfig, func() error, error) {
	return nil, nil, errNotSupported
}

type uctMD struct {
}

func openMDResource(uctHdl *dlopen.LibHandle, comp *uctComponent, mdName string, cfg *uctMDConfig) (*uctMD, func() error, error) {
	return nil, nil, errNotSupported
}

type transportDev struct {
	transport string
	device    string
}

func (d *transportDev) String() string {
	return ""
}

func (d *transportDev) isNetwork() bool {
	return false
}

func getMDTransportDevices(uctHdl *dlopen.LibHandle, md *uctMD) ([]*transportDev, error) {
	return nil, errNotSupported
}
