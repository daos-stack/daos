//
// (C) Copyright 2019 Intel Corporation.
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
// +build linux,amd64
//

package netdetect

/*
#cgo CFLAGS: -I${SRCDIR}/../../include
#cgo LDFLAGS: -lhwloc -lfabric
#include <stdlib.h>
#include <hwloc.h>
#include <stdio.h>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_errno.h>
*/
import "C"

import (
	"fmt"
	"net"
	"strings"
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
)

const (
	libFabricMajorVersion = 1
	libFabricMinorVersion = 7
	hwlocFlagsStandard    = 0
	hwlocFlagsWholeSystem = 1
)

// DeviceAffinity describes the essential details of a device and its NUMA affinity
type DeviceAffinity struct {
	DeviceName string
	CPUSet     string
	NodeSet    string
	NUMANode   int
}

// FabricScan data encapsulates the results of the fabric scanning
type FabricScan struct {
	Provider   string
	DeviceName string
	NUMANode   int
	Priority   int
}

func (fs *FabricScan) String() string {
	return fmt.Sprintf("\tfabric_iface: %s\n\tprovider: %s\n\tpinned_numa_node: %d", fs.DeviceName, fs.Provider, fs.NUMANode)
}

// DeviceScan caches initialization data for later hwloc usage
type DeviceScan struct {
	topology             C.hwloc_topology_t
	depth                C.int
	numObj               C.uint
	systemDeviceNames    []string
	systemDeviceNamesMap map[string]struct{}
	hwlocDeviceNames     []string
	hwlocDeviceNamesMap  map[string]struct{}
	targetDevice         string
}

type logger interface {
	Debug(string)
	Debugf(string, ...interface{})
	Info(string)
	Infof(string, ...interface{})
	Error(string)
	Errorf(string, ...interface{})
}

var log logger = logging.NewStdoutLogger("netdetect")

// SetLogger sets the package-level logger
func SetLogger(l logger) {
	log = l
}

func (da *DeviceAffinity) String() string {
	return fmt.Sprintf("%s:%s:%s:%d", da.DeviceName, da.CPUSet, da.NodeSet, da.NUMANode)
}

// initLib initializes the hwloc library.
// supports hwlocFlagsStandard and hwlocFlagsWholeSystem
// See hwloc.h for details on HWLOC_TOPOLOGY_FLAG_WHOLE_SYSTEM
func initLib(flags int) (C.hwloc_topology_t, error) {
	var topology C.hwloc_topology_t
	var hwlocFlags C.ulong
	status := C.hwloc_topology_init(&topology)
	if status != 0 {
		return nil, errors.New("hwloc_topology_init failure")
	}

	if flags != 0 {
		if flags == hwlocFlagsWholeSystem {
			flags = C.HWLOC_TOPOLOGY_FLAG_WHOLE_SYSTEM
			log.Debug("Setting HWLOC_TOPOLOGY_FLAG_WHOLE_SYSTEM")
		} else {
			return nil, errors.New("Invalid flag provided")
		}
	}

	status = C.hwloc_topology_set_flags(topology, C.HWLOC_TOPOLOGY_FLAG_IO_DEVICES|hwlocFlags)
	if status != 0 {
		return nil, errors.New("hwloc_topology_set_flags failure")
	}

	status = C.hwloc_topology_load(topology)
	if status != 0 {
		return nil, errors.New("hwloc_topology_load failure")
	}
	return topology, nil
}

// cleanUp closes out the hwloc resources
func cleanUp(topology C.hwloc_topology_t) {
	if topology != nil {
		C.hwloc_topology_destroy(topology)
	}
}

// getHwlocDeviceNames retrieves all of the system device names that hwloc knows about
func getHwlocDeviceNames(deviceScanCfg DeviceScan) ([]string, error) {
	var i C.uint
	var hwlocDeviceNames []string

	if deviceScanCfg.topology == nil {
		return hwlocDeviceNames, errors.New("hwloc was not initialized")
	}

	for i = 0; i < deviceScanCfg.numObj; i++ {
		node := C.hwloc_get_obj_by_depth(deviceScanCfg.topology, C.uint(deviceScanCfg.depth), i)
		if node == nil {
			continue
		}
		hwlocDeviceNames = append(hwlocDeviceNames, C.GoString(node.name))
	}

	return hwlocDeviceNames, nil
}

// initDeviceScan initializes the hwloc library and initializes DeviceScan entries
// that are frequently used for hwloc queries
func initDeviceScan() (DeviceScan, error) {
	var deviceScanCfg DeviceScan

	topology, err := initLib(hwlocFlagsStandard)
	if err != nil {
		log.Infof("Error from initLib %v", err)
		return deviceScanCfg,
			errors.New("unable to initialize hwloc library")
	}
	deviceScanCfg.topology = topology

	depth := C.hwloc_get_type_depth(topology, C.HWLOC_OBJ_OS_DEVICE)
	if depth != C.HWLOC_TYPE_DEPTH_OS_DEVICE {
		return deviceScanCfg,
			errors.New("hwloc_get_type_depth returned invalid value")
	}
	deviceScanCfg.depth = depth

	numObj := C.hwloc_get_nbobjs_by_depth(topology, C.uint(depth))
	deviceScanCfg.numObj = numObj

	// Create the list of all the valid network device names
	systemDeviceNames, err := GetDeviceNames()
	if err != nil {
		return deviceScanCfg, err
	}
	deviceScanCfg.systemDeviceNames = systemDeviceNames

	deviceScanCfg.systemDeviceNamesMap = make(map[string]struct{})
	for _, deviceName := range deviceScanCfg.systemDeviceNames {
		deviceScanCfg.systemDeviceNamesMap[deviceName] = struct{}{}
	}

	deviceScanCfg.hwlocDeviceNames, err = getHwlocDeviceNames(deviceScanCfg)
	if err != nil {
		return deviceScanCfg, errors.New("unable to obtain hwloc I/O device names")
	}

	deviceScanCfg.hwlocDeviceNamesMap = make(map[string]struct{})
	for _, hwlocDeviceName := range deviceScanCfg.hwlocDeviceNames {
		deviceScanCfg.hwlocDeviceNamesMap[hwlocDeviceName] = struct{}{}
	}

	return deviceScanCfg, err
}

const (
	direct  = iota
	sibling = iota
	bestfit = iota
)

// getLookupMethod is used to determine how to perform a match between the device we are looking for
// and the devices that are reported by hwloc.
func getLookupMethod(deviceScanCfg DeviceScan) int {

	// If the target device is on both the systemDeviceNames list and the hwlocDeviceNames list,
	// then we know we can do a valid direct lookup.  If it only shows up on the systemDevicesNames list
	// it is likely a device we do not want used, such as "lo"
	if _, found := deviceScanCfg.systemDeviceNamesMap[deviceScanCfg.targetDevice]; found {
		if _, found := deviceScanCfg.hwlocDeviceNamesMap[deviceScanCfg.targetDevice]; found {
			return direct
		}
	}
	// If not on the systemDeviceNames list, but it is on the hwlocDeviceNames list, it could be
	// virtual device name such as hfi1_0, which requires a sibling lookup to find the matching device (i.e. ib0)
	if _, found := deviceScanCfg.hwlocDeviceNamesMap[deviceScanCfg.targetDevice]; found {
		return sibling
	}

	// If not on the hwlocDeviceNames list, then we might be looking at a decorated device name such
	// as hfi1_0-dgram.  In that case, it will match to hfi1_0 where a sibling lookup will find the matching
	// device for ib0.
	return bestfit
}

// getNodeDirect finds a node object that is a direct exact match lookup of the target device against the hwloc device list
func getNodeDirect(deviceScanCfg DeviceScan) C.hwloc_obj_t {
	var node C.hwloc_obj_t
	var i C.uint

	for i = 0; i < deviceScanCfg.numObj; i++ {
		node = C.hwloc_get_obj_by_depth(deviceScanCfg.topology, C.uint(deviceScanCfg.depth), i)
		if node == nil {
			continue
		}

		if C.GoString(node.name) == deviceScanCfg.targetDevice {
			return node
		}
	}
	return node
}

// getNodeSibling finds a node object that is the sibling of the device being matched.  The sibling will be something found
// on the systemDevicesNameMap.
func getNodeSibling(deviceScanCfg DeviceScan) C.hwloc_obj_t {
	var node C.hwloc_obj_t

	node = getNodeDirect(deviceScanCfg)
	if node.parent == nil {
		return nil
	}
	// This node will have a sibling if its parent has more than one child (arity > 0)
	// Search the sibling node for one that has a name on the systemDeviceNamesMap
	// For example, hfi1_0 is a valid hwloc node.name, but it is not found on the systemDeviceNameMap.
	// The sibling of hfi1_0 is ib0, and ib0 *is* found on the systemDeviceNameMap.
	// The sibling device has the same non-I/O ancestor and shares the same NUMA Node, so we want that.
	if node.parent.arity > 0 {
		childNode := (*[1 << 30]C.hwloc_obj_t)(unsafe.Pointer(node.parent.children))
		var j C.uint
		for j = 0; j < C.uint(node.parent.arity); j++ {
			if _, found := deviceScanCfg.systemDeviceNamesMap[C.GoString(childNode[j].name)]; found {
				node = childNode[j]
				return node
			}
		}
	}
	return node
}

// getNodeBestFit finds a node object that most closely matches the name of the target device being matched.
// In order to succeed, one or more hwlocDeviceNames must be a subset of the target device name.
// This allows us to find a decorated virtual device name such as "hfi1_0-dgram" which matches against one of the hwlocDeviceNames
// which can then be mapped to a sibling, "ib0" which is found on the systemDeviceNamesMap.
func getNodeBestFit(deviceScanCfg DeviceScan) C.hwloc_obj_t {
	var node C.hwloc_obj_t
	var newDeviceName string

	// Find the largest hwloc device name that is contained by the target device name to ensure the closest match
	for _, deviceName := range deviceScanCfg.hwlocDeviceNames {
		if strings.Contains(deviceScanCfg.targetDevice, deviceName) {
			if len(deviceName) > len(newDeviceName) {
				newDeviceName = deviceName
			}
		}
	}
	if len(newDeviceName) > 0 {
		deviceScanCfg.targetDevice = newDeviceName
		if _, found := deviceScanCfg.systemDeviceNamesMap[deviceScanCfg.targetDevice]; !found {
			node = getNodeSibling(deviceScanCfg)
		} else {
			node = getNodeDirect(deviceScanCfg)
		}
	}
	return node
}

// getNUMASocketID determines the NUMA ID for the given device node.
// In an unrestricted system, hwloc can directly query for a NUMA type ancestor
// node, which directly reveals the NUMA ID.  However, when run on a restricted system,
// there may be no NUMA ancestor node available.  In this case, we must iterate through
// all NUMA nodes in the toplogy to find one that has a cpuset that intersects with
// the cpuset of the given device node to determine where it belongs.
func getNUMASocketID(topology C.hwloc_topology_t, node C.hwloc_obj_t) (int, error) {
	var numanode C.hwloc_obj_t
	var ancestorNode C.hwloc_obj_t
	var i C.uint

	if node == nil {
		return 0, errors.New("invalid node provided")
	}

	if C.GoString(node.name) == "lo" {
		return 0, nil
	}

	numanode = C.hwloc_get_ancestor_obj_by_type(topology, C.HWLOC_OBJ_NUMANODE, node)
	if numanode != nil {
		return int(C.uint(numanode.logical_index)), nil
	}

	// If we made it here, it means that we're running in some flavor of a restricted environment
	// which caused the numa node ancestor lookup to fail.  We're not restricted from looking at the
	// cpuset for each numa node, so we can look for an intersection of the node's cpuset with each
	// numa node cpuset until a match is found or we run out of candidates.
	ancestorNode = C.hwloc_get_non_io_ancestor_obj(topology, node)
	if ancestorNode == nil {
		return 0, errors.New("unable to find non-io ancestor node for device")
	}

	depth := C.hwloc_get_type_depth(topology, C.HWLOC_OBJ_NUMANODE)
	numObj := C.hwloc_get_nbobjs_by_depth(topology, C.uint(depth))
	for i = 0; i < numObj; i++ {
		numanode = C.hwloc_get_obj_by_depth(topology, C.uint(depth), i)
		if node == nil {
			continue
		}
		if C.hwloc_bitmap_isincluded(ancestorNode.allowed_cpuset, numanode.allowed_cpuset) != 0 {
			return int(C.uint(numanode.logical_index)), nil
		}
	}
	return 0, errors.New("unable to determine the NUMA socket ID")
}

// GetAffinityForNetworkDevices searches the system topology reported by hwloc
// for the devices specified by deviceNames and returns the corresponding
// name, cpuset, nodeset and NUMA node information for each device it finds.
//
// The input deviceNames []string specifies names of each network device to
// search for. Typical network device names are "eth0", "eth1", "ib0", etc.
//
// The DeviceAffinity DeviceName matches the deviceNames strings and should be
// used to help match an input device with the output.
//
// Network device names that are not found in the topology are ignored.
// The order of network devices in the return string depends on the natural
// order in the system topology and does not depend on the order specified
// by the input string.
func GetAffinityForNetworkDevices(deviceNames []string) ([]DeviceAffinity, error) {
	var affinity []DeviceAffinity
	var node C.hwloc_obj_t
	var i C.uint
	var cpuset *C.char
	var nodeset *C.char

	topology, err := initLib(hwlocFlagsStandard)
	if err != nil {
		log.Debugf("Error from initLib %v", err)
		return nil,
			errors.New("unable to initialize hwloc library")
	}

	depth := C.hwloc_get_type_depth(topology, C.HWLOC_OBJ_OS_DEVICE)
	if depth != C.HWLOC_TYPE_DEPTH_OS_DEVICE {
		return nil,
			errors.New("hwloc_get_type_depth returned invalid value")
	}

	netNames := make(map[string]struct{})
	for _, deviceName := range deviceNames {
		netNames[deviceName] = struct{}{}
	}

	numObj := C.hwloc_get_nbobjs_by_depth(topology, C.uint(depth))
	// for any OS object found in the network device list,
	// detect and store the cpuset and nodeset of the ancestor node
	// containing this object
	for i = 0; i < numObj; i++ {
		node = C.hwloc_get_obj_by_depth(topology, C.uint(depth), i)
		if node == nil {
			continue
		}
		log.Debugf("Found: %s", C.GoString(node.name))
		if _, found := netNames[C.GoString(node.name)]; !found {
			continue
		}
		ancestorNode := C.hwloc_get_non_io_ancestor_obj(topology, node)
		if ancestorNode == nil {
			continue
		}
		cpusetLen := C.hwloc_bitmap_asprintf(&cpuset,
			ancestorNode.cpuset)
		nodesetLen := C.hwloc_bitmap_asprintf(&nodeset,
			ancestorNode.nodeset)

		numaSocket, err := getNUMASocketID(topology, node)
		if err != nil {
			numaSocket = 0
		}

		if cpusetLen > 0 && nodesetLen > 0 {
			deviceNode := DeviceAffinity{
				DeviceName: C.GoString(node.name),
				CPUSet:     C.GoString(cpuset),
				NodeSet:    C.GoString(nodeset),
				NUMANode:   numaSocket,
			}
			affinity = append(affinity, deviceNode)
		}
		C.free(unsafe.Pointer(cpuset))
		C.free(unsafe.Pointer(nodeset))
	}
	cleanUp(topology)
	return affinity, nil
}

// GetAffinityForDevice searches the system topology reported by hwloc
// for the device specified by netDeviceName and returns the corresponding
// name, cpuset, nodeset, and NUMA node ID information.
//
// Call initDeviceScan() to initialize the hwloc library and then set the
// deviceScanCfg.targetDevice to a network device name prior to calling this function.
//
// The input deviceScanCfg netDeviceName string specifies a network device to
// search for. Typical network device names are "eth0", "eth1", "ib0", etc.
// The name may also be a virtual network device name such as "hfi1_0" or a
// decorated virtual network device name such as "hfi1_0-dgram".  These virtual
// network devices are valid hwloc device names, and are used by libfabric,
// but they are not valid network device names for libraries such as CART.
// CART requires one of the system device names.  Therefore, virtual device names
// are converted to system device names when it can be determined.
//
// The DeviceAffinity NUMANode describes the logical NUMA node that this device
// is closest to.  Note that, if administrator cgroup settings exclude any NUMA Node resources
// that cover the particular network device, then the NUMA node ID may be inaccurate.
func GetAffinityForDevice(deviceScanCfg DeviceScan) ([]DeviceAffinity, error) {
	var affinity []DeviceAffinity
	var node C.hwloc_obj_t
	var cpuset *C.char
	var nodeset *C.char

	if deviceScanCfg.topology == nil {
		return affinity, errors.New("hwloc libary not yet initialized")
	}

	if deviceScanCfg.targetDevice == "" {
		return affinity, errors.New("no network device name specified")
	}

	// The loopback device isn't a physical device that hwloc will find in the topology
	// If "lo" is specified, it is treated specially and be given NUMA node 0.
	if deviceScanCfg.targetDevice == "lo" {
		deviceNode := DeviceAffinity{
			DeviceName: "lo",
			CPUSet:     "0x0",
			NodeSet:    "0x1",
			NUMANode:   0,
		}
		affinity = append(affinity, deviceNode)
		return affinity, nil
	}

	lookupMethod := getLookupMethod(deviceScanCfg)
	switch lookupMethod {
	case direct:
		node = getNodeDirect(deviceScanCfg)
	case sibling:
		node = getNodeSibling(deviceScanCfg)
	case bestfit:
		node = getNodeBestFit(deviceScanCfg)
	default:
		node = getNodeBestFit(deviceScanCfg)
	}

	if node == nil {
		return affinity, errors.New(fmt.Sprintf("unable to find a system device matching: %s", deviceScanCfg.targetDevice))
	}

	ancestorNode := C.hwloc_get_non_io_ancestor_obj(deviceScanCfg.topology, node)
	if ancestorNode == nil {
		return affinity, errors.New(fmt.Sprintf("unable to find an ancestor node in topology for device for device: %s", deviceScanCfg.targetDevice))
	}

	cpusetLen := C.hwloc_bitmap_asprintf(&cpuset, ancestorNode.cpuset)
	if cpusetLen <= 0 {
		return affinity, errors.New(fmt.Sprintf("there was no cpuset available for device: %s", deviceScanCfg.targetDevice))
	}

	nodesetLen := C.hwloc_bitmap_asprintf(&nodeset, ancestorNode.nodeset)
	if nodesetLen <= 0 {
		return affinity, errors.New(fmt.Sprintf("there was no nodeset available for device: %s", deviceScanCfg.targetDevice))
	}

	numaSocket, err := getNUMASocketID(deviceScanCfg.topology, node)
	if err != nil {
		return affinity, err
	}

	deviceNode := DeviceAffinity{
		DeviceName: C.GoString(node.name),
		CPUSet:     C.GoString(cpuset),
		NodeSet:    C.GoString(nodeset),
		NUMANode:   numaSocket,
	}

	affinity = append(affinity, deviceNode)

	C.free(unsafe.Pointer(cpuset))
	C.free(unsafe.Pointer(nodeset))

	return affinity, nil
}

// GetDeviceNames examines the network interfaces
// and returns a []string identifying them by name
func GetDeviceNames() ([]string, error) {
	networkInterfaces, err := net.Interfaces()

	if err != nil {
		log.Infof("error while detecting network interfaces: %s", err)
		return nil, err
	}

	netNames := make([]string, 0, len(networkInterfaces))
	for _, i := range networkInterfaces {
		netNames = append(netNames, i.Name)
	}
	return netNames, nil
}

// MercuryToLibFabric converts a Mercury fabric provider string into a libfabric compatible provider string
// Returns null string if the Mercury provider has no known equivalent
func MercuryToLibFabric(provider string) string {
	switch provider {
	case "ofi+sockets":
		return "sockets"
	case "ofi+verbs":
		return "verbs"
	case "ofi_rxm":
		return "ofi_rxm"
	case "ofi+psm2":
		return "psm2"
	case "ofi+gni":
		return "gni"
	default:
	}
	return ""
}

// LibFabricToMercury convert a libfabric provider string into a Mercury compatible provider string
// Returns null string if the libfabric provider has no known equivalent
func LibFabricToMercury(provider string) string {
	switch provider {
	case "sockets":
		return "ofi+sockets"
	case "verbs":
		return "ofi+verbs"
	case "ofi_rxm":
		return "ofi_rxm"
	case "psm2":
		return "ofi+psm2"
	case "gni":
		return "ofi+gni"
	default:
	}
	return ""
}

// ValidateNetworkConfigStub is used for most unit testing to replace ValidateNetworkConfig because the network configuration
// validation depends upon physical hardware resources and configuration on the target machine
// that are either not known or static in the test environment
func ValidateNetworkConfigStub(provider string, device string, numaNode int) (bool, error) {
	return true, nil
}

// ValidateNetworkConfig confirms that the given network device supports the chosen provider
// and that the device matches the NUMA ID given.
// If everything matches, the result is true.  Otherwise, false.
func ValidateNetworkConfig(provider string, device string, numaNode int) (bool, error) {
	var fi *C.struct_fi_info
	var hints *C.struct_fi_info
	var libfabricProviderList string

	hints = C.fi_allocinfo()
	defer C.fi_freeinfo(fi)

	if provider == "" {
		return false, errors.New("provider required")
	}

	if device == "" {
		return false, errors.New("device required")
	}

	log.Debugf("Input provider string: %s", provider)
	// convert the Mercury provider string into a libfabric provider string
	// to aid in matching it against the libfabric providers
	tmp := strings.Split(provider, ";")
	for _, subProvider := range tmp {
		libFabricProvider := MercuryToLibFabric(subProvider)
		if len(libFabricProvider) > 0 {
			libfabricProviderList += libFabricProvider + ";"
		} else {
			log.Debugf("Provider '%s' is not known by libfabric.", subProvider)
			return false, errors.New(fmt.Sprintf("Fabric provider: %s not known by libfabric.  Use 'daos_server network list' to view supported providers", subProvider))
		}
	}
	libfabricProviderList = strings.TrimSuffix(libfabricProviderList, ";")
	hints.fabric_attr.prov_name = C.strdup(C.CString(libfabricProviderList))

	C.fi_getinfo(C.uint(libFabricMajorVersion<<16|libFabricMinorVersion), nil, nil, 0, hints, &fi)
	deviceScanCfg, err := initDeviceScan()
	if err != nil {
		return false, errors.New(fmt.Sprintf("unable to initialize device scan:  Error: %v", err))
	}
	defer cleanUp(deviceScanCfg.topology)

	if _, found := deviceScanCfg.systemDeviceNamesMap[device]; !found {
		return false, errors.New(fmt.Sprintf("device: %s is an invalid device name", device))
	}

	// iterate over the libfabric records that match this provider
	// and look for one that has a matching device name
	// The device names returned from libfabric may be device names such as "hfi1_0" that maps to a system device "ib0"
	// or may be a decorated device name such as "hfi1_0-dgram" that maps to "hfi1_0" that maps to a system device "ib0"
	// In order to find a device and provider match, the libfabric device name must be converted into a system device name.
	// GetAffinityForDevice() is used to provide this conversion.
	for ; fi != nil; fi = fi.next {
		if fi.domain_attr == nil || fi.domain_attr.name == nil {
			continue
		}
		if fi.fabric_attr == nil || fi.fabric_attr.prov_name == nil {
			continue
		}
		deviceScanCfg.targetDevice = C.GoString(fi.domain_attr.name)
		deviceAffinity, err := GetAffinityForDevice(deviceScanCfg)
		if err == nil && len(deviceAffinity) > 0 {
			if deviceAffinity[0].DeviceName == device {
				log.Debugf("Device %s supports provider: %s", device, provider)
				if deviceAffinity[0].NUMANode != numaNode {
					log.Debugf("The NUMA node for device %s does not match the provided value %d.  Performance degradation may result. "+
						"Remove the pinned_numa_node value from daos_server.yml then execute 'daos_server network scan' "+
						"to see the valid NUMA node associated with the network device", device, numaNode)
					return false, nil
				}
				log.Debugf("The NUMA node for device %s matches the provided value %d.  Network configuration is valid.", device, numaNode)
				return true, nil
			}
		}
	}
	log.Debugf("Configuration error!  Device %s does not support provider: %s", device, provider)
	return false, nil
}

// ScanFabric examines libfabric data to find the network devices that support the given fabric provider.
func ScanFabric(provider string) ([]FabricScan, error) {
	var ScanResults []FabricScan
	var fi *C.struct_fi_info
	var hints *C.struct_fi_info
	var devCount int
	var libfabricProviderList string
	var mercuryProviderList string
	hints = C.fi_allocinfo()
	defer C.fi_freeinfo(fi)

	resultsMap := make(map[string]struct{})

	if hints == nil {
		return ScanResults, errors.New("enable to allocate memory for libfabric query")
	}

	// If a provider was given, then set the libfabric search hint to match the provider
	if provider != "" {
		log.Debugf("Input provider string: %s", provider)
		tmp := strings.Split(provider, ";")
		for i, subProvider := range tmp {
			log.Debugf("Mercury provider %d is %s", i, subProvider)
			libFabricProvider := MercuryToLibFabric(subProvider)
			if len(libFabricProvider) > 0 {
				libfabricProviderList += libFabricProvider + ";"
			} else {
				log.Debugf("Provider '%s' is not known by libfabric.", subProvider)
				return ScanResults, errors.New(fmt.Sprintf("Fabric provider: %s not known by libfabric. Try 'daos_server network list' to view available providers", subProvider))
			}
		}
		libfabricProviderList = strings.TrimSuffix(libfabricProviderList, ";")
		log.Debugf("Final libFabricProviderList is %s", libfabricProviderList)
		hints.fabric_attr.prov_name = C.strdup(C.CString(libfabricProviderList))
	}

	// Initialize libfabric and perform the scan
	C.fi_getinfo(C.uint(libFabricMajorVersion<<16|libFabricMinorVersion), nil, nil, 0, hints, &fi)
	if fi == nil {
		return ScanResults, errors.New("libfabric found no records matching the specified provider")
	}

	// We have some data from libfabric scan.  Let's initialize hwloc and get the device scan started
	deviceScanCfg, err := initDeviceScan()
	if err != nil {
		return ScanResults, err
	}
	defer cleanUp(deviceScanCfg.topology)

	log.Debugf("initDeviceScan completed.  Depth %d, numObj %d, systemDeviceNames %v, hwlocDeviceNames %v",
		deviceScanCfg.depth, deviceScanCfg.numObj, deviceScanCfg.systemDeviceNames, deviceScanCfg.hwlocDeviceNames)

	for ; fi != nil; fi = fi.next {
		mercuryProviderList = ""
		if fi.domain_attr == nil || fi.domain_attr.name == nil {
			continue
		}
		if fi.fabric_attr == nil || fi.fabric_attr.prov_name == nil {
			continue
		}

		deviceScanCfg.targetDevice = C.GoString(fi.domain_attr.name)
		deviceAffinity, err := GetAffinityForDevice(deviceScanCfg)
		if err != nil {
			log.Debugf("Error from GetAffinityForDevice: %v", err)
			continue
		}

		tmp := strings.Split(C.GoString(fi.fabric_attr.prov_name), ";")
		for _, subProvider := range tmp {
			mercuryProvider := LibFabricToMercury(subProvider)
			if len(mercuryProvider) > 0 {
				mercuryProviderList += mercuryProvider + ";"
			}
		}
		mercuryProviderList = strings.TrimSuffix(mercuryProviderList, ";")

		scanResults := FabricScan{
			Provider:   mercuryProviderList,
			DeviceName: deviceAffinity[0].DeviceName,
			NUMANode:   deviceAffinity[0].NUMANode,
			Priority:   devCount,
		}

		results := scanResults.String()
		if _, found := resultsMap[results]; !found {
			resultsMap[results] = struct{}{}
			log.Debugf("\n%s", results)
			ScanResults = append(ScanResults, scanResults)
			devCount++
		}
	}
	return ScanResults, nil
}
