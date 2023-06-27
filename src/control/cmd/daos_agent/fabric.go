//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"fmt"
	"net"
	"sort"
	"sync"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

// FabricNotFoundErr is the error returned when no appropriate fabric interface
// was found.
func FabricNotFoundErr(netDevClass hardware.NetDevClass) error {
	return fmt.Errorf("no suitable fabric interface found of type %q", netDevClass)
}

// FabricInterface represents a generic fabric interface.
type FabricInterface struct {
	Name        string
	Domain      string
	NetDevClass hardware.NetDevClass
	hw          *hardware.FabricInterface
}

func (f *FabricInterface) Providers() []string {
	provs := f.hw.Providers.ToSlice()
	provStrs := make([]string, len(provs))
	for i, p := range provs {
		provStrs[i] = p.Name
	}
	return provStrs
}

func (f *FabricInterface) String() string {
	var dom string
	if f.Domain != "" {
		dom = "/" + f.Domain
	}
	return fmt.Sprintf("%s%s (%s)", f.Name, dom, f.NetDevClass)
}

// HasProvider determines if the FabricInterface supports a given provider.
func (f *FabricInterface) HasProvider(provider string) bool {
	return f.hw.SupportsProvider(provider)
}

// FabricDevClassManual is a wildcard netDevClass that indicates the device was
// supplied by the user.
const FabricDevClassManual = hardware.NetDevClass(1 << 31)

// addrFI is a fabric interface that can provide its addresses.
type addrFI interface {
	Addrs() ([]net.Addr, error)
}

// NUMAFabric represents a set of fabric interfaces organized by NUMA node.
type NUMAFabric struct {
	log   logging.Logger
	mutex sync.RWMutex

	numaMap map[int][]*FabricInterface

	currentNumaDevIdx map[int]int // current device idx to use on each NUMA node
	currentNUMANode   int         // current NUMA node to search
	ignoreIfaces      common.StringSet

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

// WithIgnoredDevices adds a set of fabric interface names that should be ignored when
// selecting a device.
func (n *NUMAFabric) WithIgnoredDevices(ifaces common.StringSet) *NUMAFabric {
	n.ignoreIfaces = ifaces
	if len(ifaces) > 0 {
		n.log.Tracef("ignoring fabric devices: %s", n.ignoreIfaces)
	}
	return n
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
func (n *NUMAFabric) GetDevice(numaNode int, netDevClass hardware.NetDevClass, provider string) (*FabricInterface, error) {
	if n == nil {
		return nil, errors.New("nil NUMAFabric")
	}

	if provider == "" {
		return nil, errors.New("provider is required")
	}

	n.mutex.Lock()
	defer n.mutex.Unlock()

	fi, err := n.getDeviceFromNUMA(numaNode, netDevClass, provider)
	if err == nil {
		return copyFI(fi, provider), nil
	}

	fi, err = n.findOnAnyNUMA(netDevClass, provider)
	if err != nil {
		return nil, err
	}

	return copyFI(fi, provider), nil
}

func copyFI(fi *FabricInterface, provider string) *FabricInterface {
	fiCopy := new(FabricInterface)
	*fiCopy = *fi
	return fiCopy
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

func (n *NUMAFabric) getDeviceFromNUMA(numaNode int, netDevClass hardware.NetDevClass, provider string) (*FabricInterface, error) {
	for checked := 0; checked < n.getNumDevices(numaNode); checked++ {
		fabricIF := n.getNextDevice(numaNode)

		if n.ignoreIfaces.Has(fabricIF.Name) {
			n.log.Tracef("device %s: ignored (ignore list %s)", fabricIF, n.ignoreIfaces)
			continue
		}

		// Manually-provided interfaces can be assumed to support what's needed by the system.
		if fabricIF.NetDevClass != FabricDevClassManual {
			if fabricIF.NetDevClass != netDevClass {
				n.log.Tracef("device %s: excluded (netDevClass %s != %s)", fabricIF, fabricIF.NetDevClass, netDevClass)
				continue
			}

			if !fabricIF.HasProvider(provider) {
				n.log.Tracef("device %s: excluded (provider %s not supported)", fabricIF, provider)
				continue
			}
		}

		if err := n.validateDevice(fabricIF); err != nil {
			n.log.Noticef("device %s: excluded (%s)", fabricIF, err)
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
		n.log.Tracef("device %s: %s/%s", fi.Name, a.Network(), a.String())
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

func (n *NUMAFabric) findOnAnyNUMA(netDevClass hardware.NetDevClass, provider string) (*FabricInterface, error) {
	nodes := n.getNUMANodes()
	numNodes := len(nodes)

	for i := 0; i < numNodes; i++ {
		n.currentNUMANode = (n.currentNUMANode + 1) % numNodes
		fi, err := n.getDeviceFromNUMA(nodes[n.currentNUMANode], netDevClass, provider)
		if err == nil {
			n.log.Tracef("device %s: selected on NUMA node %d)", fi, n.currentNUMANode)
			return fi, nil
		}
	}
	return nil, FabricNotFoundErr(netDevClass)
}

func (n *NUMAFabric) getNUMANodes() []int {
	keys := make([]int, 0)
	for k := range n.numaMap {
		keys = append(keys, k)
	}
	sort.Ints(keys)
	return keys
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
func NUMAFabricFromScan(ctx context.Context, log logging.Logger, scan *hardware.FabricInterfaceSet) *NUMAFabric {
	fabric := newNUMAFabric(log)

	for _, name := range scan.Names() {
		fi, err := scan.GetInterface(name)
		if err != nil {
			log.Errorf("unexpected failure getting FI %q from scan: %s", name, err.Error())
			continue
		}

		newIFs := fabricInterfacesFromHardware(fi)

		for _, newIF := range newIFs {
			numa := int(fi.NUMANode)
			fabric.Add(numa, newIF)

			log.Tracef("device %s: [%d] added to NUMA node %d", newIF, fabric.NumDevices(numa)-1, numa)
		}
	}

	if fabric.NumNUMANodes() == 0 {
		log.Notice("no network devices detected in fabric scan")
	}

	return fabric
}

func fabricInterfacesFromHardware(fi *hardware.FabricInterface) []*FabricInterface {
	fis := make([]*FabricInterface, 0)
	for netIF := range fi.NetInterfaces {
		newFI := &FabricInterface{
			Name:        netIF,
			NetDevClass: fi.DeviceClass,
			hw:          fi,
		}

		if fi.Name != netIF {
			newFI.Domain = fi.Name
		}

		fis = append(fis, newFI)
	}

	return fis
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

	return fabric
}
