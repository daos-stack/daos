//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"fmt"
	"net"
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
	Name        string
	Domain      string
	NetDevClass uint32
	Providers   []string
}

func (f *FabricInterface) String() string {
	var dom string
	if f.Domain != "" {
		dom = "/" + f.Domain
	}
	return fmt.Sprintf("%s%s (%s)", f.Name, dom, netdetect.DevClassName(f.NetDevClass))
}

// AddProvider adds a provider to the FabricInterface.
func (f *FabricInterface) AddProvider(provider string) {
	if f == nil || provider == "" {
		return
	}

	for _, p := range f.Providers {
		// Avoid adding duplicates
		if p == provider {
			return
		}
	}

	f.Providers = append(f.Providers, provider)
}

// FabricDevClassManual is a wildcard netDevClass that indicates the device was
// supplied by the user.
const FabricDevClassManual = uint32(1 << 31)

// addrFI is a fabric interface that can provide its addresses.
type addrFI interface {
	Addrs() ([]net.Addr, error)
}

// NUMAFabric represents a set of fabric interfaces organized by NUMA node.
type NUMAFabric struct {
	log   logging.Logger
	mutex sync.RWMutex

	numaMap map[int][]*FabricInterface

	// current device idx to use on each NUMA node
	currentNumaDevIdx map[int]int
	defaultNumaNode   int

	getAddrInterface func(name string) (addrFI, error)
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

func (n *NUMAFabric) getTotalNumDevices() int {
	total := 0
	for _, devs := range n.numaMap {
		total += len(devs)
	}
	return total
}

// GetDevice selects the next available interface device on the requested NUMA node.
func (n *NUMAFabric) GetDevice(numaNode int, netDevClass uint32) (*FabricInterface, error) {
	if n == nil {
		return nil, errors.New("nil NUMAFabric")
	}

	n.mutex.Lock()
	defer n.mutex.Unlock()

	// If the only device is the loopback, it may not match the server's requested type, but
	// we can try it anyway, in case the server is listening on the same machine.
	if n.getTotalNumDevices() == 1 {
		if fi, err := n.getDeviceFromNUMA(0, netdetect.Loopback); err == nil {
			if netDevClass != netdetect.Loopback {
				n.log.Errorf("%s is the only fabric interface available, requested type=%s",
					fi.String(), netdetect.DevClassName(netDevClass))
			}
			return fi, nil
		}
	}

	fi, err := n.getDeviceFromNUMA(numaNode, netDevClass)
	if err == nil {
		return fi, nil
	}

	fi, err = n.findOnAnyNUMA(netDevClass)
	if err != nil {
		return nil, err
	}

	fiCopy := new(FabricInterface)
	*fiCopy = *fi
	return fiCopy, nil
}

// Find finds a specific fabric device by name.
func (n *NUMAFabric) Find(name string) (*FabricInterface, error) {
	if n == nil {
		return nil, errors.New("nil NUMAFabric")
	}

	for _, devs := range n.numaMap {
		for _, fi := range devs {
			if fi.Name == name {
				return fi, nil
			}
		}
	}
	return nil, fmt.Errorf("fabric interface %q not found", name)
}

func (n *NUMAFabric) getDeviceFromNUMA(numaNode int, netDevClass uint32) (*FabricInterface, error) {
	for checked := 0; checked < n.getNumDevices(numaNode); checked++ {
		fabricIF := n.getNextDevice(numaNode)

		if fabricIF.NetDevClass != netDevClass && fabricIF.NetDevClass != FabricDevClassManual {
			n.log.Debugf("Excluding device: %s, network device class: %s. Does not match requested network device class: %s",
				fabricIF.Name, netdetect.DevClassName(fabricIF.NetDevClass), netdetect.DevClassName(netDevClass))
			continue
		}

		if err := n.validateDevice(fabricIF); err != nil {
			n.log.Infof("Excluding device %q: %s", fabricIF.Name, err.Error())
			continue
		}

		return fabricIF, nil
	}
	return nil, FabricNotFoundErr(netDevClass)
}

// getAddrFI wraps net.InterfaceByName to allow using the addrFI interface as
// the return value.
func getAddrFI(name string) (addrFI, error) {
	return net.InterfaceByName(name)
}

func (n *NUMAFabric) validateDevice(fi *FabricInterface) error {
	if n.getAddrInterface == nil {
		n.getAddrInterface = getAddrFI
	}

	addrInterface, err := n.getAddrInterface(fi.Name)
	if err != nil {
		return err
	}

	addrs, err := addrInterface.Addrs()
	if err != nil {
		return err
	}

	for _, a := range addrs {
		n.log.Debugf("Interface: %s, Addr: %s %s", fi.Name, a.Network(), a.String())
		if ipAddr, isIP := a.(*net.IPNet); isIP && ipAddr.IP != nil && !ipAddr.IP.IsUnspecified() {
			return nil
		}
	}

	return fmt.Errorf("no IP addresses for fabric interface %s", fi.Name)
}

func (n *NUMAFabric) getNextDevice(numaNode int) *FabricInterface {
	idx := n.getNextDevIndex(numaNode)
	return n.numaMap[numaNode][idx]
}

func (n *NUMAFabric) findOnAnyNUMA(netDevClass uint32) (*FabricInterface, error) {
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

func (n *NUMAFabric) setDefaultNUMANode() {
	for numa := range n.numaMap {
		n.defaultNumaNode = numa
		n.log.Debugf("The default NUMA node is: %d", numa)
		break
	}
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
		if curIF, err := fabric.Find(fs.DeviceName); err == nil {
			curIF.AddProvider(fs.Provider)
			continue
		}

		newIF := &FabricInterface{
			Name:        fs.DeviceName,
			NetDevClass: fs.NetDevClass,
			Providers:   []string{fs.Provider},
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

	fabric.setDefaultNUMANode()

	if fabric.NumNUMANodes() == 0 {
		log.Info("No network devices detected in fabric scan\n")
	}

	return fabric
}

// NUMAFabricFromConfig generates a NUMAFabric layout based on a config.
func NUMAFabricFromConfig(log logging.Logger, cfg []*NUMAFabricConfig) *NUMAFabric {
	fabric := newNUMAFabric(log)

	for _, fc := range cfg {
		node := fc.NUMANode
		for _, fi := range fc.Interfaces {
			fabric.numaMap[node] = append(fabric.numaMap[node],
				&FabricInterface{
					Name:        fi.Interface,
					Domain:      fi.Domain,
					NetDevClass: FabricDevClassManual,
				})
		}
	}
	fabric.setDefaultNUMANode()

	return fabric
}
