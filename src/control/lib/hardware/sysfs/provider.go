//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package sysfs

import (
	"context"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

// NewProvider creates a new SysfsProvider.
func NewProvider(log logging.Logger) *Provider {
	return &Provider{
		root: "/sys",
		log:  log,
	}
}

// SysfsProvider provides system information from sysfs.
type Provider struct {
	log  logging.Logger
	root string
}

func (s *Provider) getRoot() string {
	if s.root == "" {
		s.root = "/sys"
	}
	return s.root
}

func (s *Provider) sysPath(pathElem ...string) string {
	pathElem = append([]string{s.getRoot()}, pathElem...)

	return filepath.Join(pathElem...)
}

// GetNetDevClass fetches the network device class for the given network interface.
func (s *Provider) GetNetDevClass(dev string) (hardware.NetDevClass, error) {
	if dev == "" {
		return 0, errors.New("device name required")
	}

	devClass, err := ioutil.ReadFile(s.sysPath("class", "net", dev, "type"))
	if err != nil {
		return 0, err
	}

	res, err := strconv.Atoi(strings.TrimSpace(string(devClass)))
	return hardware.NetDevClass(res), err
}

// GetTopology builds a minimal topology of network devices from the contents of sysfs.
func (s *Provider) GetTopology(ctx context.Context) (*hardware.Topology, error) {
	if s == nil {
		return nil, errors.New("sysfs provider is nil")
	}

	topo := &hardware.Topology{}

	err := filepath.Walk(s.sysPath("devices"), func(path string, fi os.FileInfo, err error) error {
		if fi == nil {
			return nil
		}

		if err != nil {
			return err
		}

		if s.isVirtual(path) {
			// skip virtual devices
			return nil
		}

		subsystem, err := s.getSubsystemClass(path)
		if err != nil {
			// Current directory does not represent a device
			return nil
		}

		var dev *hardware.PCIDevice
		switch subsystem {
		case "net", "infiniband", "cxi":
			dev, err = s.getNetworkDevice(path)
			if err != nil {
				s.log.Debug(err.Error())
				return nil
			}
		default:
			return nil
		}

		numaID, err := s.getNUMANode(path)
		if err != nil {
			s.log.Debug(err.Error())
			return nil
		}

		pciAddr, err := s.getPCIAddress(path)
		if err != nil {
			s.log.Debug(err.Error())
			return nil
		}
		dev.PCIAddr = *pciAddr

		s.log.Debugf("adding device found at %q (type %s, NUMA node %d)", path, dev.Type, numaID)

		topo.AddDevice(uint(numaID), dev)

		return nil
	})

	if err == io.EOF || err == nil {
		return topo, nil
	}
	return nil, err
}

func (s *Provider) isVirtual(path string) bool {
	return strings.HasPrefix(path, s.sysPath("devices", "virtual"))
}

func (s *Provider) getSubsystemClass(path string) (string, error) {
	subsysPath, err := filepath.EvalSymlinks(filepath.Join(path, "subsystem"))
	if err != nil {
		return "", errors.Wrap(err, "couldn't get subsystem data")
	}

	return filepath.Base(subsysPath), nil
}

func (s *Provider) getNetworkDevice(path string) (*hardware.PCIDevice, error) {
	// Network devices will have the device/net subdirectory structure
	netDev, err := ioutil.ReadDir(filepath.Join(path, "device", "net"))
	if err != nil {
		return nil, errors.Wrapf(err, "failed to read net device")
	}

	if len(netDev) == 0 {
		return nil, errors.Errorf("no network device for %q", filepath.Base(path))
	}

	devName := filepath.Base(path)
	netDevName := netDev[0].Name()

	var devType hardware.DeviceType
	if netDevName == devName {
		devType = hardware.DeviceTypeNetInterface
	} else {
		devType = hardware.DeviceTypeOFIDomain
	}

	return &hardware.PCIDevice{
		Name: devName,
		Type: devType,
	}, nil
}

func (s *Provider) getNUMANode(path string) (uint, error) {
	numaPath := filepath.Join(path, "device", "numa_node")
	numaBytes, err := ioutil.ReadFile(numaPath)
	if err != nil {
		return 0, errors.Wrapf(err, "couldn't read %q", numaPath)
	}
	numaStr := strings.TrimSpace(string(numaBytes))

	numaID, err := strconv.Atoi(numaStr)
	if err != nil || numaID < 0 {
		s.log.Debugf("invalid NUMA node ID %q, using NUMA node 0", numaStr)
		numaID = 0
	}
	return uint(numaID), nil
}

func (s *Provider) getPCIAddress(path string) (*hardware.PCIAddress, error) {
	pciPath, err := filepath.EvalSymlinks(filepath.Join(path, "device"))
	if err != nil {
		return nil, errors.Wrap(err, "couldn't get PCI device")
	}

	pciAddr, err := hardware.NewPCIAddress(filepath.Base(pciPath))
	if err != nil {
		return nil, errors.Wrapf(err, "%q not parsed as PCI address", pciAddr)
	}

	return pciAddr, nil
}
