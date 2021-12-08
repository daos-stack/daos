//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package sysfs

import (
	"errors"
	"io/ioutil"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/daos-stack/daos/src/control/lib/hardware"
)

// NewProvider creates a new SysfsProvider.
func NewProvider() *Provider {
	return &Provider{
		root: "/sys",
	}
}

// SysfsProvider provides system information from sysfs.
type Provider struct {
	root string
}

// GetNetDevClass fetches the network device class for the given network interface.
func (s *Provider) GetNetDevClass(dev string) (hardware.NetDevClass, error) {
	if dev == "" {
		return 0, errors.New("device name required")
	}

	if s.root == "" {
		s.root = "/sys"
	}

	devClass, err := ioutil.ReadFile(filepath.Join(s.root, "class", "net", dev, "type"))
	if err != nil {
		return 0, err
	}

	res, err := strconv.Atoi(strings.TrimSpace(string(devClass)))
	return hardware.NetDevClass(res), err
}
