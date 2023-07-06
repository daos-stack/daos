//
// (C) Copyright 2021-2023 Intel Corporation.
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

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

var netSubsystems = []string{"cxi", "infiniband", "net"}

const (
	cxiProvider     = "ofi+cxi"
	netvscSubsystem = "net"
	netvscDriver    = "hv_netvsc"
)

func isNetwork(subsystem string) bool {
	for _, netSubsystem := range netSubsystems {
		if subsystem == netSubsystem {
			return true
		}
	}

	return false
}

func isNetvscDevice(path string, subsystem string) bool {
	if subsystem != netvscSubsystem {
		return false
	}

	content, err := ioutil.ReadFile(filepath.Join(path, "device", "uevent"))
	if err != nil {
		return false
	}

	val, err := common.FindKeyValue(strings.Split(string(content), "\n"), "DRIVER")
	return err == nil && val == netvscDriver
}

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

// GetTopology builds a topology from the contents of sysfs.
func (s *Provider) GetTopology(ctx context.Context) (*hardware.Topology, error) {
	if s == nil {
		return nil, errors.New("sysfs provider is nil")
	}

	topo := &hardware.Topology{}

	for _, subsystem := range s.topologySubsystems() {
		if err := s.addSubsystemDevices(topo, subsystem); err != nil {
			return nil, err
		}
	}

	if err := s.addVirtualNetDevices(topo); err != nil {
		return nil, err
	}

	return topo, nil
}

func (s *Provider) topologySubsystems() []string {
	// For now we only fetch network devices from sysfs.
	return netSubsystems
}

func (s *Provider) addSubsystemDevices(topo *hardware.Topology, subsystem string) error {
	var netvscPaths []string
	subsysRoot := s.sysPath("class", subsystem)
	err := filepath.Walk(subsysRoot, func(path string, fi os.FileInfo, err error) error {
		if fi == nil {
			return nil
		}

		if err != nil {
			return err
		}

		if path == subsysRoot {
			return nil
		}

		if isNetvscDevice(path, subsystem) {
			netvscPaths = append(netvscPaths, path)
			return nil
		}

		return s.addPCIDevice(topo, subsystem, path)
	})

	if err != io.EOF && err != nil {
		return err
	}

	for _, path := range netvscPaths {
		if err := s.addNetvscDevice(topo, path); err != nil {
			return err
		}
	}

	return nil
}

func (s *Provider) addPCIDevice(topo *hardware.Topology, subsystem string, path string) error {
	if !isNetwork(subsystem) {
		return nil
	}

	dev, err := s.getNetworkDevice(path, subsystem)
	if err != nil {
		s.log.Tracef(err.Error())
		return nil
	}

	numaID, err := s.getNUMANode(path)
	if err != nil {
		s.log.Tracef("using default NUMA node, unable to get: %s", err.Error())
		numaID = 0
	}

	pciAddr, err := s.getPCIAddress(path)
	if err != nil {
		s.log.Trace(err.Error())
		return nil
	}
	dev.PCIAddr = *pciAddr

	s.log.Tracef("adding device found at %q (type %s, NUMA node %d)", path, dev.Type, numaID)
	return topo.AddDevice(uint(numaID), dev)
}

func (s *Provider) getNetworkDevice(path, subsystem string) (*hardware.PCIDevice, error) {
	// Network devices will have the device/net subdirectory structure
	netDev, err := ioutil.ReadDir(filepath.Join(path, "device", "net"))
	if err != nil {
		return nil, errors.Wrapf(err, "failed to read net device")
	}

	if len(netDev) == 0 {
		return nil, errors.Errorf("no network device for %q", filepath.Base(path))
	}

	devName := filepath.Base(path)

	var devType hardware.DeviceType
	if subsystem == "net" {
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
		return 0, err
	}
	numaStr := strings.TrimSpace(string(numaBytes))

	numaID, err := strconv.Atoi(numaStr)
	if err != nil || numaID < 0 {
		s.log.Tracef("invalid NUMA node ID %q, using NUMA node 0", numaStr)
		numaID = 0
	}
	return uint(numaID), nil
}

func (s *Provider) getPCIAddress(path string) (*hardware.PCIAddress, error) {
	pciPath, err := filepath.EvalSymlinks(filepath.Join(path, "device"))
	if err != nil {
		return nil, errors.Wrap(err, "couldn't get PCI device")
	}

	var pciAddr *hardware.PCIAddress
	for pciPath != s.sysPath("devices") {
		pciAddr, err = hardware.NewPCIAddress(filepath.Base(pciPath))
		if err == nil {
			return pciAddr, nil
		}

		pciPath = filepath.Dir(pciPath)
	}

	return nil, errors.Errorf("unable to parse PCI address from %q", path)
}

func (s *Provider) addNetvscDevice(topo *hardware.Topology, path string) error {
	ifaceName := filepath.Base(path)

	virt := &hardware.VirtualDevice{
		Name: ifaceName,
		Type: hardware.DeviceTypeNetInterface,
	}

	backingDev, err := s.getBackingDevice(path, topo.AllDevices())
	if err != nil {
		s.log.Noticef("Skipping NetVSC network adapter %q: physical backing device not found",
			ifaceName)
		return nil
	}
	s.log.Tracef("NetVSC network adapter %q has physical backing device %q",
		ifaceName, backingDev.DeviceName())
	virt.BackingDevice = backingDev

	s.log.Tracef("Adding NetVSC network adapter at %q", path)
	topo.VirtualDevices = append(topo.VirtualDevices, virt)

	return nil
}

func (s *Provider) addVirtualNetDevices(topo *hardware.Topology) error {
	virtualDevices := make([]*hardware.VirtualDevice, 0)
	addedDevices := topo.AllDevices()

	netPath := s.sysPath("devices", "virtual", "net")
	netIfaces, err := ioutil.ReadDir(netPath)
	if err != nil {
		s.log.Tracef("unable to read any virtual net interfaces: %s", err.Error())
		return nil
	}

	for _, iface := range netIfaces {
		virt := &hardware.VirtualDevice{
			Name: iface.Name(),
			Type: hardware.DeviceTypeNetInterface,
		}

		ifacePath := filepath.Join(netPath, iface.Name())

		if backingDev, err := s.getBackingDevice(ifacePath, addedDevices); err == nil {
			s.log.Tracef("virtual device %q has physical backing device %q", iface.Name(), backingDev.DeviceName())
			virt.BackingDevice = backingDev
		}

		s.log.Tracef("adding virtual device at %q", ifacePath)
		virtualDevices = append(virtualDevices, virt)
	}

	if len(virtualDevices) > 0 {
		if len(topo.VirtualDevices) == 0 {
			topo.VirtualDevices = make([]*hardware.VirtualDevice, 0)
		}
		topo.VirtualDevices = append(topo.VirtualDevices, virtualDevices...)
	}

	return nil
}

func (s *Provider) getBackingDevice(ifacePath string, devices map[string]hardware.Device) (*hardware.PCIDevice, error) {
	ifaceName := filepath.Base(ifacePath)

	// NB: There are a couple of different ways a parent device may be linked from its child.
	// - File that contains the parent device name.
	// - Symlink to the parent in the format of "lower_<something>"

	parent, err := s.getParentDevName(ifaceName)
	if err == nil {
		pciDev, found := devices[parent]
		if found {
			return pciDev.PCIDevice(), nil
		}
	}

	ifaceFiles, err := ioutil.ReadDir(ifacePath)
	if err != nil {
		s.log.Tracef("unable to read contents of %s", ifacePath)
		return nil, err
	}

	for _, f := range ifaceFiles {
		// NB: For now we only look one level below for a link to a physical device.
		// In theory a virtual interface could be backed by another virtual
		// interface, which is backed by another, and so on until we reach a
		// physical one. For now we would consider such devices not to be hardware
		// backed.
		if strings.HasPrefix(f.Name(), "lower_") {
			path, err := filepath.EvalSymlinks(filepath.Join(ifacePath, f.Name()))
			if err != nil {
				s.log.Error(err.Error())
				continue
			}

			pciDev, found := devices[filepath.Base(path)]
			if found {
				return pciDev.PCIDevice(), nil
			}
		}
	}

	return nil, errors.Errorf("no backing device for %q", ifaceName)
}

func (s *Provider) getParentDevName(iface string) (string, error) {
	parentBytes, err := ioutil.ReadFile(s.sysPath("class", "net", iface, "parent"))
	if err != nil {
		return "", err
	}
	return strings.TrimSpace(string(parentBytes)), nil
}

// GetFabricInterfaces harvests fabric interfaces from sysfs.
func (s *Provider) GetFabricInterfaces(ctx context.Context, provider string) (*hardware.FabricInterfaceSet, error) {
	if s == nil {
		return nil, errors.New("sysfs provider is nil")
	}

	fiSet := hardware.NewFabricInterfaceSet()
	if provider == "" || provider == cxiProvider {
		cxiFIs, err := s.getCXIFabricInterfaces()
		if err != nil {
			return nil, err
		}

		fiSet = hardware.NewFabricInterfaceSet(cxiFIs...)
	}
	return fiSet, nil
}

func (s *Provider) getCXIFabricInterfaces() ([]*hardware.FabricInterface, error) {
	cxiDevs, err := ioutil.ReadDir(s.sysPath("class", "cxi"))
	if os.IsNotExist(err) {
		s.log.Tracef("no cxi subsystem in sysfs")
		return []*hardware.FabricInterface{}, nil
	} else if err != nil {
		return nil, err
	}

	if len(cxiDevs) == 0 {
		s.log.Tracef("no cxi devices in sysfs")
		return []*hardware.FabricInterface{}, nil
	}

	cxiFIs := make([]*hardware.FabricInterface, 0)
	for _, dev := range cxiDevs {
		cxiFIs = append(cxiFIs, &hardware.FabricInterface{
			Name:      dev.Name(),
			OSName:    dev.Name(),
			Providers: hardware.NewFabricProviderSet(&hardware.FabricProvider{Name: "ofi+cxi"}),
		})
	}

	return cxiFIs, nil
}

// GetNetDevState fetches the state of a network interface.
func (s *Provider) GetNetDevState(iface string) (hardware.NetDevState, error) {
	if s == nil {
		return hardware.NetDevStateUnknown, errors.New("sysfs provider is nil")
	}

	if iface == "" {
		return hardware.NetDevStateUnknown, errors.New("fabric interface name is required")
	}

	devClass, err := s.GetNetDevClass(iface)
	if err != nil {
		return hardware.NetDevStateUnknown, errors.Wrapf(err, "can't determine device class for %q", iface)
	}

	switch devClass {
	case hardware.Infiniband:
		return s.getInfinibandDevState(iface)
	case hardware.Loopback:
		return s.getLoopbackDevState(iface)
	default:
		return s.getNetOperState(iface)
	}
}

func (s *Provider) getLoopbackDevState(iface string) (hardware.NetDevState, error) {
	state, err := s.getNetOperState(iface)
	if err != nil {
		return hardware.NetDevStateUnknown, err
	}

	// Loopback devices with state unknown are up
	if state == hardware.NetDevStateUnknown {
		return hardware.NetDevStateReady, nil
	}
	return state, nil
}

func (s *Provider) getNetOperState(iface string) (hardware.NetDevState, error) {
	statePath := s.sysPath("class", "net", iface, "operstate")
	stateBytes, err := ioutil.ReadFile(statePath)
	if err != nil {
		return hardware.NetDevStateUnknown, errors.Wrapf(err, "failed to get %q operational state", iface)
	}

	stateStr := strings.ToLower(strings.TrimSpace(string(stateBytes)))

	// Operational states as described in kernel docs:
	// https://www.kernel.org/doc/html/latest/networking/operstates.html#tlv-ifla-operstate
	var state hardware.NetDevState
	switch stateStr {
	case "up":
		state = hardware.NetDevStateReady
	case "down", "lowerlayerdown", "notpresent":
		// down: Interface is unable to transfer data on L1, f.e. ethernet is not plugged or
		//       interface is ADMIN down.
		// lowerlayerdown: Interfaces stacked on an interface that is down (f.e. VLAN).
		// notpresent: Interface is physically not present. Typically the kernel hides them
		//             but the status is included in code.
		state = hardware.NetDevStateDown
	case "testing", "dormant":
		// testing: Interface is in testing mode, for example executing driver self-tests or
		//          media (cable) test. It can’t be used for normal traffic until tests complete.
		// dormant: Interface is L1 up, but waiting for an external event, f.e. for a protocol
		//          to establish. (802.1X)
		state = hardware.NetDevStateNotReady
	default:
		state = hardware.NetDevStateUnknown
	}
	return state, nil
}

func (s *Provider) getInfinibandDevState(iface string) (hardware.NetDevState, error) {
	if s.isVirtualNetIface(iface) {
		// Virtual IB devices should have a parent device whose status applies to the child.
		parent, err := s.getParentDevName(iface)
		if err == nil {
			return s.getInfinibandDevState(parent)
		}
		// If we don't find the parent, we can try reading the status directly if available
	}

	// The best way to determine that an Infiniband interface is ready is to check the state
	// of its ports. Ports in the "ACTIVE" state are either fully ready or will be very soon.
	ibPath := s.sysPath("class", "net", iface, "device", "infiniband")
	ibDevs, err := ioutil.ReadDir(ibPath)
	if err != nil {
		return hardware.NetDevStateUnknown, errors.Wrapf(err, "can't access Infiniband details for %q", iface)
	}

	ibDevState := make([]hardware.NetDevState, 0)
	for _, dev := range ibDevs {
		portPath := filepath.Join(ibPath, dev.Name(), "ports")
		ports, err := ioutil.ReadDir(portPath)
		if err != nil {
			return hardware.NetDevStateUnknown, errors.Wrapf(err, "unable to get ports for %s/%s", iface, dev.Name())
		}

		portState := make([]hardware.NetDevState, 0)
		for _, port := range ports {
			statePath := filepath.Join(portPath, port.Name(), "state")
			stateBytes, err := ioutil.ReadFile(statePath)
			if err != nil {
				return hardware.NetDevStateUnknown, errors.Wrapf(err, "unable to get state for %s/%s port %s",
					iface, dev.Name(), port.Name())
			}

			portState = append(portState, s.ibStateToNetDevState(string(stateBytes)))
		}

		ibDevState = append(ibDevState, condenseNetDevState(portState))
	}

	return condenseNetDevState(ibDevState), nil
}

func (s *Provider) isVirtualNetIface(iface string) bool {
	virtPath := s.sysPath("devices", "virtual", "net", iface)

	_, err := os.Stat(virtPath)
	return err == nil
}

// Infiniband state enum is derived from ibstat:
// https://github.com/linux-rdma/rdma-core/blob/master/infiniband-diags/ibstat.c
const (
	ibStateUnknown = 0
	ibStateDown    = 1
	ibStateInit    = 2
	ibStateArmed   = 3
	ibStateActive  = 4
)

func (s *Provider) ibStateToNetDevState(stateStr string) hardware.NetDevState {
	stateSubstrs := strings.Split(string(stateStr), ": ")
	stateNum, err := strconv.Atoi(stateSubstrs[0])
	if err != nil {
		s.log.Noticef("unable to parse IB state %q: %s", stateStr, err.Error())
		return hardware.NetDevStateUnknown
	}

	switch stateNum {
	case ibStateDown:
		return hardware.NetDevStateDown
	case ibStateActive:
		return hardware.NetDevStateReady
	case ibStateArmed, ibStateInit:
		return hardware.NetDevStateNotReady
	default:
		return hardware.NetDevStateUnknown
	}
}

// condenseNetDevState uses a set of states to determine an overall state.
func condenseNetDevState(states []hardware.NetDevState) hardware.NetDevState {
	condensed := hardware.NetDevStateUnknown
	for _, state := range states {
		switch state {
		case hardware.NetDevStateReady:
			// Device is overall ready if:
			// - at least one port is ready
			// - no ports are not ready
			if condensed != hardware.NetDevStateNotReady {
				condensed = hardware.NetDevStateReady
			}
		case hardware.NetDevStateDown:
			// Device is overall down if all ports are down. If any port is in any other
			// state, the device cannot be considered down.
			if condensed == hardware.NetDevStateUnknown {
				condensed = hardware.NetDevStateDown
			}
		case hardware.NetDevStateNotReady:
			// Device is not ready if any port is not ready.
			condensed = hardware.NetDevStateNotReady
		}
	}

	return condensed
}

// IsIOMMUEnabled checks whether IOMMU is enabled by interrogating files in sysfs and implements
// the IOMMUDetector interface on sysfs provider.
func (s *Provider) IsIOMMUEnabled() (bool, error) {
	if s == nil {
		return false, errors.New("sysfs provider is nil")
	}

	// Simple test for now -- if the path exists and contains
	// DMAR entries, we assume that's good enough.
	dmars, err := ioutil.ReadDir(s.sysPath("class", "iommu"))
	if err != nil && !os.IsNotExist(err) {
		return false, err
	}

	return err == nil && len(dmars) > 0, nil
}
