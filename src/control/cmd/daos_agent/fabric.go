//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"fmt"
	"sync"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
)

// FabricNotFoundErr is the error returned when no appropriate fabric interface
// was found.
func FabricNotFoundErr(netDevClass uint32) error {
	return fmt.Errorf("no suitable fabric interface found of type %q", netdetect.DevClassName(netDevClass))
}

// FabricInterface represents a generic fabric interface.
type FabricInterface struct {
	Name        string `yaml:"name"`
	Domain      string `yaml:"domain"`
	NetDevClass uint32 `yaml:"net_dev_class"`
}

// DefaultFabricInterface is the one used if no devices are found on the system.
var DefaultFabricInterface = &FabricInterface{
	Name:   "lo",
	Domain: "lo",
}

// NUMAFabric represents a set of fabric interfaces organized by NUMA node.
type NUMAFabric struct {
	log   logging.Logger
	mutex sync.RWMutex

	numaMap map[int][]*FabricInterface

	// current device idx to use on each NUMA node
	currentNumaDevIdx map[int]int
	defaultNumaNode   int
}

// Add adds a fabric interface to a specific NUMA node.
func (n *NUMAFabric) Add(numaNode int, fi *FabricInterface) error {
	if n == nil {
		return errors.New("nil NUMAFabric")
	}

	n.mutex.Lock()
	defer n.mutex.Unlock()

	n.numaMap[numaNode] = append(n.numaMap[numaNode], fi)
	return nil
}

// NumDevices gets the number of devices on a given NUMA node.
func (n *NUMAFabric) NumDevices(numaNode int) int {
	if n == nil {
		return 0
	}

	n.mutex.RLock()
	defer n.mutex.RUnlock()

	return n.getNumDevices(numaNode)
}

func (n *NUMAFabric) getNumDevices(numaNode int) int {
	if devs, exist := n.numaMap[numaNode]; exist {
		return len(devs)
	}
	return 0
}

// NumNUMANodes gets the number of NUMA nodes.
func (n *NUMAFabric) NumNUMANodes() int {
	if n == nil {
		return 0
	}

	n.mutex.RLock()
	defer n.mutex.RUnlock()

	return n.getNumNUMANodes()
}

func (n *NUMAFabric) getNumNUMANodes() int {
	return len(n.numaMap)
}

// GetDevice selects the next available interface device on the requested NUMA node.
func (n *NUMAFabric) GetDevice(numaNode int, netDevClass uint32) (*FabricInterface, error) {
	if n == nil {
		return nil, errors.New("nil NUMAFabric")
	}

	n.mutex.Lock()
	defer n.mutex.Unlock()

	if n.getNumNUMANodes() == 0 {
		n.log.Infof("No fabric interfaces found, using default interface %q", DefaultFabricInterface.Name)
		return DefaultFabricInterface, nil
	}

	fi, err := n.getDeviceFromNUMA(numaNode, netDevClass)
	if err == nil {
		return fi, nil
	}

	fi, err = n.findOnRemoteNUMA(netDevClass)
	if err != nil {
		return nil, err
	}

	fiCopy := new(FabricInterface)
	*fiCopy = *fi
	return fiCopy, nil
}

func (n *NUMAFabric) getDeviceFromNUMA(numaNode int, netDevClass uint32) (*FabricInterface, error) {
	for checked := 0; checked < n.getNumDevices(numaNode); checked++ {
		fabricIF := n.getNextDevice(numaNode)

		if fabricIF.NetDevClass != netDevClass {
			n.log.Debugf("Excluding device: %s, network device class: %s from attachInfoCache. Does not match network device class: %s",
				fabricIF.Name, netdetect.DevClassName(fabricIF.NetDevClass), netdetect.DevClassName(netDevClass))
			continue
		}

		return fabricIF, nil
	}
	return nil, FabricNotFoundErr(netDevClass)
}

func (n *NUMAFabric) getNextDevice(numaNode int) *FabricInterface {
	idx := n.getNextDevIndex(numaNode)
	return n.numaMap[numaNode][idx]
}

func (n *NUMAFabric) findOnRemoteNUMA(netDevClass uint32) (*FabricInterface, error) {
	numNodes := n.getNumNUMANodes()
	for i := 0; i < numNodes; i++ {
		numa := (n.defaultNumaNode + i) % numNodes
		fi, err := n.getDeviceFromNUMA(numa, netDevClass)
		if err == nil {
			// Start the search on this node next time
			n.defaultNumaNode = numa
			n.log.Debugf("Suitable fabric interface %q found on NUMA node %d", fi.Name, numa)
			return fi, nil
		}
	}
	return nil, FabricNotFoundErr(netDevClass)
}

// getNextDevIndex is a simple round-robin load balancing scheme
// for NUMA nodes that have multiple adapters to choose from.
func (n *NUMAFabric) getNextDevIndex(numaNode int) int {
	if n.currentNumaDevIdx == nil {
		n.currentNumaDevIdx = make(map[int]int)
	}
	numDevs := n.getNumDevices(numaNode)
	if numDevs > 0 {
		deviceIndex := n.currentNumaDevIdx[numaNode]
		n.currentNumaDevIdx[numaNode] = (deviceIndex + 1) % numDevs
		return deviceIndex
	}

	// Unreachable -- callers looping on n.getNumDevices()
	panic(fmt.Sprintf("no fabric interfaces on NUMA node %d", numaNode))
}

func newNUMAFabric(log logging.Logger) *NUMAFabric {
	return &NUMAFabric{
		log:               log,
		numaMap:           make(map[int][]*FabricInterface),
		currentNumaDevIdx: make(map[int]int),
	}
}

// NUMAFabricFromScan generates a NUMAFabric from a fabric scan result.
func NUMAFabricFromScan(ctx context.Context, log logging.Logger, scan []*netdetect.FabricScan,
	getDevAlias func(ctx context.Context, devName string) (string, error)) *NUMAFabric {
	fabric := newNUMAFabric(log)

	for _, fs := range scan {
		if fs.DeviceName == "lo" {
			continue
		}

		newIF := &FabricInterface{
			Name:        fs.DeviceName,
			NetDevClass: fs.NetDevClass,
		}

		if getDevAlias != nil {
			deviceAlias, err := getDevAlias(ctx, newIF.Name)
			if err != nil {
				log.Debugf("Non-fatal error: failed to get device alias for %q: %v", newIF.Name, err)
			} else {
				newIF.Domain = deviceAlias
			}
		}

		numa := int(fs.NUMANode)
		fabric.Add(numa, newIF)

		log.Debugf("Added device %q, domain %q for NUMA %d, device number %d",
			newIF.Name, newIF.Domain, numa, fabric.NumDevices(numa)-1)
	}

	for numa := range fabric.numaMap {
		fabric.defaultNumaNode = numa
		log.Debugf("The default NUMA node is: %d", numa)
		break
	}

	if fabric.NumNUMANodes() == 0 {
		log.Info("No network devices detected in fabric scan\n")
	}

	return fabric
}
