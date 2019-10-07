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
	direct = iota
	sibling
	bestfit
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
	NUMANode   uint
}

// FabricScan data encapsulates the results of the fabric scanning
type FabricScan struct {
	Provider   string
	DeviceName string
	NUMANode   uint
	Priority   int
}

func (fs FabricScan) String() string {
	return fmt.Sprintf("\tfabric_iface: %v\n\tprovider: %v\n\tpinned_numa_node: %d", fs.DeviceName, fs.Provider, fs.NUMANode)
}

// DeviceScan caches initialization data for later hwloc usage
type DeviceScan struct {
	topology             C.hwloc_topology_t
	depth                int
	numObj               uint
	systemDeviceNames    []string
	systemDeviceNamesMap map[string]struct{}
	hwlocDeviceNames     []string
	hwlocDeviceNamesMap  map[string]struct{}
	targetDevice         string
}

type logger interface {
	Debug(string)
	Debugf(string, ...interface{})
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
		return nil, errors.Errorf("hwloc_topology_init failure: %v", status)
	}

	switch flags {
	case hwlocFlagsStandard:
		hwlocFlags = hwlocFlagsStandard
	case hwlocFlagsWholeSystem:
		log.Debug("Setting HWLOC_TOPOLOGY_FLAG_WHOLE_SYSTEM")
		hwlocFlags = hwlocFlagsWholeSystem
	default:
		// Call cleanUp because we failed after hwloc_topology_init succeeded.
		cleanUp(topology)
		return nil, errors.Errorf("Invalid flag provided: %v", flags)
	}

	status = C.hwloc_topology_set_flags(topology, C.HWLOC_TOPOLOGY_FLAG_IO_DEVICES|hwlocFlags)
	if status != 0 {
		// Call cleanUp because we failed after hwloc_topology_init succeeded.
		cleanUp(topology)
		return nil, errors.Errorf("hwloc_topology_set_flags failure: %v", status)
	}

	status = C.hwloc_topology_load(topology)
	if status != 0 {
		// Call cleanUp because we failed after hwloc_topology_init succeeded.
		cleanUp(topology)
		return nil, errors.Errorf("hwloc_topology_load failure: %v", status)
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
	var hwlocDeviceNames []string
	var i uint

	if deviceScanCfg.topology == nil {
		return hwlocDeviceNames, errors.New("hwloc was not initialized")
	}

	for i = 0; i < deviceScanCfg.numObj; i++ {
		node := C.hwloc_get_obj_by_depth(deviceScanCfg.topology, C.uint(deviceScanCfg.depth), C.uint(i))
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
		log.Debugf("Error from initLib %v", err)
		return deviceScanCfg,
			errors.New("unable to initialize hwloc library")
	}
	deviceScanCfg.topology = topology

	depth := C.hwloc_get_type_depth(topology, C.HWLOC_OBJ_OS_DEVICE)
	if depth != C.HWLOC_TYPE_DEPTH_OS_DEVICE {
		defer cleanUp(deviceScanCfg.topology)
		return deviceScanCfg,
			errors.New("hwloc_get_type_depth returned invalid value")
	}
	deviceScanCfg.depth = int(depth)

	deviceScanCfg.numObj = uint(C.hwloc_get_nbobjs_by_depth(topology, C.uint(depth)))
	if deviceScanCfg.numObj == 0 {
		defer cleanUp(deviceScanCfg.topology)
		return deviceScanCfg,
			errors.New("hwloc_get_nbobjs_by_depth returned invalid value: no OS devices found")
	}

	// Create the list of all the valid network device names
	systemDeviceNames, err := GetDeviceNames()
	if err != nil {
		defer cleanUp(deviceScanCfg.topology)
		return deviceScanCfg, err
	}
	deviceScanCfg.systemDeviceNames = systemDeviceNames

	deviceScanCfg.systemDeviceNamesMap = make(map[string]struct{})
	for _, deviceName := range deviceScanCfg.systemDeviceNames {
		deviceScanCfg.systemDeviceNamesMap[deviceName] = struct{}{}
	}

	deviceScanCfg.hwlocDeviceNames, err = getHwlocDeviceNames(deviceScanCfg)
	if err != nil {
		defer cleanUp(deviceScanCfg.topology)
		return deviceScanCfg, errors.New("unable to obtain hwloc I/O device names")
	}

	deviceScanCfg.hwlocDeviceNamesMap = make(map[string]struct{})
	for _, hwlocDeviceName := range deviceScanCfg.hwlocDeviceNames {
		deviceScanCfg.hwlocDeviceNamesMap[hwlocDeviceName] = struct{}{}
	}

	return deviceScanCfg, err
}

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
	var i uint

	for i = 0; i < deviceScanCfg.numObj; i++ {
		node := C.hwloc_get_obj_by_depth(deviceScanCfg.topology, C.uint(deviceScanCfg.depth), C.uint(i))
		if node == nil {
			continue
		}

		if C.GoString(node.name) == deviceScanCfg.targetDevice {
			return node
		}
	}
	return nil
}

// getNodeSibling finds a node object that is the sibling of the device being matched.  The sibling will be something found
// on the systemDevicesNameMap.
func getNodeSibling(deviceScanCfg DeviceScan) C.hwloc_obj_t {

	node := getNodeDirect(deviceScanCfg)
	if node == nil || node.parent == nil {
		return nil
	}
	// This node will have a sibling if its parent has more than one child (arity > 0)
	// Search the sibling node for one that has a name on the systemDeviceNamesMap
	// For example, hfi1_0 is a valid hwloc node.name, but it is not found on the systemDeviceNameMap.
	// The sibling of hfi1_0 is ib0, and ib0 *is* found on the systemDeviceNameMap.
	// The sibling device has the same non-I/O ancestor and shares the same NUMA Node, so we want that.
	if node.parent.arity > 0 {
		count := C.uint(node.parent.arity)
		children := (*[1 << 30]C.hwloc_obj_t)(unsafe.Pointer(node.parent.children))[:count:count]
		for _, child := range children {
			if _, found := deviceScanCfg.systemDeviceNamesMap[C.GoString(child.name)]; found {
				return child
			}
		}
	}
	return nil
}

// getNodeBestFit finds a node object that most closely matches the name of the target device being matched.
// In order to succeed, one or more hwlocDeviceNames must be a subset of the target device name.
// This allows us to find a decorated virtual device name such as "hfi1_0-dgram" which matches against one of the hwlocDeviceNames
// which can then be mapped to a sibling, "ib0" which is found on the systemDeviceNamesMap.
func getNodeBestFit(deviceScanCfg DeviceScan) C.hwloc_obj_t {
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
			return getNodeSibling(deviceScanCfg)
		} else {
			return getNodeDirect(deviceScanCfg)
		}
	}
	return nil
}

// getNUMASocketID determines the NUMA ID for the given device node.
// In an unrestricted system, hwloc can directly query for a NUMA type ancestor
// node, which directly reveals the NUMA ID.  However, when run on a restricted system,
// there may be no NUMA ancestor node available.  In this case, we must iterate through
// all NUMA nodes in the toplogy to find one that has a cpuset that intersects with
// the cpuset of the given device node to determine where it belongs.
// In some configurations, the number of NUMA nodes found is 0.  In that case,
// the NUMA ID will be considered 0.
func getNUMASocketID(topology C.hwloc_topology_t, node C.hwloc_obj_t) (uint, error) {
	var i uint

	if node == nil {
		return 0, errors.New("invalid node provided")
	}

	if C.GoString(node.name) == "lo" {
		return 0, nil
	}

	numanode := C.hwloc_get_ancestor_obj_by_type(topology, C.HWLOC_OBJ_NUMANODE, node)
	if numanode != nil {
		return uint(numanode.logical_index), nil
	}

	// If we made it here, it means that we're running in some flavor of a restricted environment
	// which caused the numa node ancestor lookup to fail.  We're not restricted from looking at the
	// cpuset for each numa node, so we can look for an intersection of the node's cpuset with each
	// numa node cpuset until a match is found or we run out of candidates.
	ancestorNode := C.hwloc_get_non_io_ancestor_obj(topology, node)
	if ancestorNode == nil {
		return 0, errors.New("unable to find non-io ancestor node for device")
	}

	depth := C.hwloc_get_type_depth(topology, C.HWLOC_OBJ_NUMANODE)
	numObj := uint(C.hwloc_get_nbobjs_by_depth(topology, C.uint(depth)))
	if numObj == 0 {
		log.Debugf("NUMA Node data is unavailable.  Using NUMA 0\n")
		return 0, nil
	}

	log.Debugf("There are %d NUMA nodes.", numObj)

	for i = 0; i < numObj; i++ {
		numanode := C.hwloc_get_obj_by_depth(topology, C.uint(depth), C.uint(i))
		if numanode == nil {
			// We don't want the lack of NUMA information to be an error.
			// If we get this far and can't access the NUMA topology data,
			// we will use NUMA ID 0.
			log.Debugf("NUMA Node data is unavailable.  Using NUMA 0\n")
			return 0, nil
		}
		if C.hwloc_bitmap_isincluded(ancestorNode.allowed_cpuset, numanode.allowed_cpuset) != 0 {
			return uint(numanode.logical_index), nil
		}
	}

	log.Debugf("Unable to determine NUMA socket ID.  Using NUMA 0")
	return 0, nil
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
	var cpuset *C.char
	var nodeset *C.char
	var i uint

	topology, err := initLib(hwlocFlagsStandard)
	if err != nil {
		log.Debugf("Error from initLib %v", err)
		return nil,
			errors.New("unable to initialize hwloc library")
	}
	defer cleanUp(topology)

	depth := C.hwloc_get_type_depth(topology, C.HWLOC_OBJ_OS_DEVICE)
	if depth != C.HWLOC_TYPE_DEPTH_OS_DEVICE {
		return nil,
			errors.New("hwloc_get_type_depth returned invalid value")
	}

	netNames := make(map[string]struct{})
	for _, deviceName := range deviceNames {
		netNames[deviceName] = struct{}{}
	}

	numObj := uint(C.hwloc_get_nbobjs_by_depth(topology, C.uint(depth)))
	// for any OS object found in the network device list,
	// detect and store the cpuset and nodeset of the ancestor node
	// containing this object
	for i = 0; i < numObj; i++ {
		node := C.hwloc_get_obj_by_depth(topology, C.uint(depth), C.uint(i))
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
func GetAffinityForDevice(deviceScanCfg DeviceScan) (DeviceAffinity, error) {
	var node C.hwloc_obj_t
	var cpuset *C.char
	var nodeset *C.char

	if deviceScanCfg.topology == nil {
		return DeviceAffinity{}, errors.New("hwloc libary not yet initialized")
	}

	if deviceScanCfg.targetDevice == "" {
		return DeviceAffinity{}, errors.New("no network device name specified")
	}

	// The loopback device isn't a physical device that hwloc will find in the topology
	// If "lo" is specified, it is treated specially and be given NUMA node 0.
	if deviceScanCfg.targetDevice == "lo" {
		return DeviceAffinity{
			DeviceName: "lo",
			CPUSet:     "0x0",
			NodeSet:    "0x1",
			NUMANode:   0,
		}, nil
	}

	switch getLookupMethod(deviceScanCfg) {
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
		return DeviceAffinity{}, errors.Errorf("unable to find a system device matching: %s", deviceScanCfg.targetDevice)
	}

	ancestorNode := C.hwloc_get_non_io_ancestor_obj(deviceScanCfg.topology, node)
	if ancestorNode == nil {
		return DeviceAffinity{}, errors.Errorf("unable to find an ancestor node in topology for device for device: %s", deviceScanCfg.targetDevice)
	}

	cpusetLen := C.hwloc_bitmap_asprintf(&cpuset, ancestorNode.cpuset)
	if cpusetLen <= 0 {
		return DeviceAffinity{}, errors.Errorf("there was no cpuset available for device: %s", deviceScanCfg.targetDevice)
	}
	defer C.free(unsafe.Pointer(cpuset))

	nodesetLen := C.hwloc_bitmap_asprintf(&nodeset, ancestorNode.nodeset)
	if nodesetLen <= 0 {
		return DeviceAffinity{}, errors.Errorf("there was no nodeset available for device: %s", deviceScanCfg.targetDevice)
	}
	defer C.free(unsafe.Pointer(nodeset))

	numaSocket, err := getNUMASocketID(deviceScanCfg.topology, node)
	if err != nil {
		return DeviceAffinity{}, err
	}

	return DeviceAffinity{
		DeviceName: C.GoString(node.name),
		CPUSet:     C.GoString(cpuset),
		NodeSet:    C.GoString(nodeset),
		NUMANode:   numaSocket,
	}, nil
}

// GetDeviceNames examines the network interfaces
// and returns a []string identifying them by name
func GetDeviceNames() ([]string, error) {
	networkInterfaces, err := net.Interfaces()

	if err != nil {
		log.Debugf("error while detecting network interfaces: %s", err)
		return nil, err
	}

	netNames := make([]string, 0, len(networkInterfaces))
	for _, i := range networkInterfaces {
		netNames = append(netNames, i.Name)
	}
	return netNames, nil
}

// GetSupportedProviders returns a string containing all supported Mercury providers
func GetSupportedProviders() []string {
	return []string{"ofi+gni", "ofi+psm2", "ofi+tcp", "ofi+sockets", "ofi+verbs", "ofi_rxm"}
}

// mercuryToLibFabric converts a single Mercury fabric provider string into a libfabric compatible provider string
func mercuryToLibFabric(provider string) (string, error) {
	switch provider {
	case "ofi+sockets":
		return "sockets", nil
	case "ofi+tcp":
		return "tcp", nil
	case "ofi+verbs":
		return "verbs", nil
	case "ofi_rxm":
		return "ofi_rxm", nil
	case "ofi+psm2":
		return "psm2", nil
	case "ofi+gni":
		return "gni", nil
	default:
	}
	return "", errors.Errorf("fabric provider: %s not known by libfabric.  Use 'daos_server network list' to view supported providers", provider)
}

// convertMercuryToLibFabric converts a Mercury provider string containing one or more providers
// separated by ';' into a libfabric compatible provider string
// All subproviders must convert successfully in order to have a successful conversion.
func convertMercuryToLibFabric(provider string) (string, error) {
	var libFabricProviderList string

	if len(provider) == 0 {
		return "", errors.New("fabric provider was empty")
	}

	tmp := strings.Split(provider, ";")
	for _, subProvider := range tmp {
		libFabricProvider, err := mercuryToLibFabric(subProvider)
		if err != nil {
			return "", errors.Errorf("fabric provider: '%s' is not known by libfabric.  Use 'daos_server network list' to view supported providers", subProvider)
		}
		libFabricProviderList += libFabricProvider + ";"
	}
	return strings.TrimSuffix(libFabricProviderList, ";"), nil
}

// libFabricToMercury converts a single libfabric provider string into a Mercury compatible provider string
func libFabricToMercury(provider string) (string, error) {
	switch provider {
	case "sockets":
		return "ofi+sockets", nil
	case "tcp":
		return "ofi+tcp", nil
	case "verbs":
		return "ofi+verbs", nil
	case "ofi_rxm":
		return "ofi_rxm", nil
	case "psm2":
		return "ofi+psm2", nil
	case "gni":
		return "ofi+gni", nil
	default:
	}

	return "", errors.Errorf("fabric provider: %s not known by Mercury", provider)
}

// convertLibFabricToMercury converts a libfabric provider string containing one or more providers
// separated by ';' into a Mercury compatible provider string
// At least one provider in the list must convert to a Mercury provider in order to have success.
func convertLibFabricToMercury(provider string) (string, error) {
	var mercuryProviderList string

	log.Debugf("Converting provider string: '%s' to Mercury", provider)

	if len(provider) == 0 {
		return "", errors.New("fabric provider was empty.")
	}

	tmp := strings.Split(provider, ";")
	for _, subProvider := range tmp {
		mercuryProvider, err := libFabricToMercury(subProvider)
		// It's non-fatal if libFabricToMercury returns an error.  It just means that
		// this individual provider could not be converted.  It is only an error when
		// none of the providers can be converted.
		if err != nil {
			continue
		}
		mercuryProviderList += mercuryProvider + ";"
	}

	// Success if we converted at least one provider
	if mercuryProviderList != "" {
		return strings.TrimSuffix(mercuryProviderList, ";"), nil
	}

	return "", errors.Errorf("failed to convert the libfabric provider list '%s' to any mercury provider", provider)
}

// ValidateProviderStub is used for most unit testing to replace ValidateProviderConfig because the network configuration
// validation depends upon physical hardware resources and configuration on the target machine
// that are either not known or static in the test environment
func ValidateProviderStub(provider string, device string) error {
	// Call the full function to get the results without generating any hard errors
	err := ValidateProviderConfig(provider, device)
	if err != nil {
		log.Debugf("ValidateProviderConfig (device: %s, provider %s) returned error: %v", device, provider, err)
	}
	return nil
}

// ValidateProviderConfig confirms that the given network device supports the chosen provider
func ValidateProviderConfig(device string, provider string) error {
	var fi *C.struct_fi_info
	var hints *C.struct_fi_info

	if provider == "" {
		return errors.New("provider required")
	}

	if device == "" {
		return errors.New("device required")
	}

	log.Debugf("Input provider string: %s", provider)
	// convert the Mercury provider string into a libfabric provider string
	// to aid in matching it against the libfabric providers
	libfabricProviderList, err := convertMercuryToLibFabric(provider)
	if err != nil {
		return err
	}

	hints = C.fi_allocinfo()
	if hints == nil {
		return errors.New("unable to initialize lib fabric - failed to allocinfo")
	}
	defer C.fi_freeinfo(hints)

	hints.fabric_attr.prov_name = C.strdup(C.CString(libfabricProviderList))
	C.fi_getinfo(C.uint(libFabricMajorVersion<<16|libFabricMinorVersion), nil, nil, 0, hints, &fi)
	if fi == nil {
		return errors.New("unable to initialize lib fabric")
	}
	defer C.fi_freeinfo(fi)

	deviceScanCfg, err := initDeviceScan()
	if err != nil {
		return errors.Errorf("unable to initialize device scan:  Error: %v", err)
	}
	defer cleanUp(deviceScanCfg.topology)

	if _, found := deviceScanCfg.systemDeviceNamesMap[device]; !found {
		return errors.Errorf("device: %s is an invalid device name", device)
	}

	// iterate over the libfabric records that match this provider
	// and look for one that has a matching device name
	// The device names returned from libfabric may be device names such as "hfi1_0" that maps to a system device "ib0"
	// or may be a decorated device name such as "hfi1_0-dgram" that maps to "hfi1_0" that maps to a system device "ib0"
	// In order to find a device and provider match, the libfabric device name must be converted into a system device name.
	// GetAffinityForDevice() is used to provide this conversion.
	for ; fi != nil; fi = fi.next {
		if fi.domain_attr == nil || fi.domain_attr.name == nil || fi.fabric_attr == nil || fi.fabric_attr.prov_name == nil {
			continue
		}
		deviceScanCfg.targetDevice = C.GoString(fi.domain_attr.name)
		deviceAffinity, err := GetAffinityForDevice(deviceScanCfg)
		if err != nil {
			continue
		}
		if deviceAffinity.DeviceName == device {
			log.Debugf("Device %s supports provider: %s", device, provider)
			return nil
		}
	}
	return errors.Errorf("Device %s does not support provider: %s", device, provider)
}

// ValidateNUMAStub is used for most unit testing to replace ValidateNUMAConfig because the network configuration
// validation depends upon physical hardware resources and configuration on the target machine
// that are either not known or static in the test environment
func ValidateNUMAStub(device string, numaNode uint) error {

	err := ValidateNUMAConfig(device, numaNode)
	if err != nil {
		log.Debugf("ValidateNUMAConfig (device: %s, NUMA: %d) returned error: %v", device, numaNode, err)
	}
	return nil
}

// ValidateNUMAConfig confirms that the given network device matches the NUMA ID given.
func ValidateNUMAConfig(device string, numaNode uint) error {
	log.Debugf("Validate network config -- given numaNode: %d", numaNode)
	if device == "" {
		return errors.New("device required")
	}

	deviceScanCfg, err := initDeviceScan()
	if err != nil {
		return errors.Errorf("unable to initialize device scan:  Error: %v", err)
	}
	defer cleanUp(deviceScanCfg.topology)

	if _, found := deviceScanCfg.systemDeviceNamesMap[device]; !found {
		return errors.Errorf("device: %s is an invalid device name", device)
	}

	deviceScanCfg.targetDevice = device
	deviceAffinity, err := GetAffinityForDevice(deviceScanCfg)
	if err != nil {
		return err
	}
	if deviceAffinity.NUMANode != numaNode {
		return errors.Errorf("The NUMA node for device %s does not match the provided value %d. "+
			"Remove the pinned_numa_node value from daos_server.yml then execute 'daos_server network scan' "+
			"to see the valid NUMA node associated with the network device", device, numaNode)
	}
	log.Debugf("The NUMA node for device %s matches the provided value %d.  Network configuration is valid.", device, numaNode)
	return nil
}

// ScanFabric examines libfabric data to find the network devices that support the given fabric provider.
func ScanFabric(provider string) ([]FabricScan, error) {
	var ScanResults []FabricScan
	var fi *C.struct_fi_info
	var hints *C.struct_fi_info
	var devCount int

	resultsMap := make(map[string]struct{})

	hints = C.fi_allocinfo()
	if hints == nil {
		return ScanResults, errors.New("unable to initialize lib fabric - failed to allocinfo")
	}
	defer C.fi_freeinfo(hints)

	// If a provider was given, then set the libfabric search hint to match the provider
	if provider != "" {
		log.Debugf("Input provider string: %s", provider)
		libfabricProviderList, err := convertMercuryToLibFabric(provider)
		if err != nil {
			return ScanResults, err
		}
		log.Debugf("Final libFabricProviderList is %s", libfabricProviderList)
		hints.fabric_attr.prov_name = C.strdup(C.CString(libfabricProviderList))
	}

	// Initialize libfabric and perform the scan
	C.fi_getinfo(C.uint(libFabricMajorVersion<<16|libFabricMinorVersion), nil, nil, 0, hints, &fi)
	if fi == nil {
		log.Debugf("libfabric found no records matching the specified provider")
		return ScanResults, nil
	}
	defer C.fi_freeinfo(fi)

	// We have some data from libfabric scan.  Let's initialize hwloc and get the device scan started
	deviceScanCfg, err := initDeviceScan()
	if err != nil {
		return ScanResults, err
	}
	defer cleanUp(deviceScanCfg.topology)

	log.Debugf("initDeviceScan completed.  Depth %d, numObj %d, systemDeviceNames %v, hwlocDeviceNames %v",
		deviceScanCfg.depth, deviceScanCfg.numObj, deviceScanCfg.systemDeviceNames, deviceScanCfg.hwlocDeviceNames)

	for ; fi != nil; fi = fi.next {
		if fi.domain_attr == nil || fi.domain_attr.name == nil || fi.fabric_attr == nil || fi.fabric_attr.prov_name == nil {
			continue
		}

		deviceScanCfg.targetDevice = C.GoString(fi.domain_attr.name)
		deviceAffinity, err := GetAffinityForDevice(deviceScanCfg)
		if err != nil {
			log.Debugf("Error from GetAffinityForDevice: %v", err)
			continue
		}

		// Convert the libfabric provider list to a Mercury compatible provider list
		mercuryProviderList, err := convertLibFabricToMercury(C.GoString(fi.fabric_attr.prov_name))
		if err != nil {
			log.Debugf("Couldn't convert the libfabric provider string: %s", C.GoString(fi.fabric_attr.prov_name))
			log.Debugf("Skipping FI record for device %s, provider %s", deviceAffinity.DeviceName, C.GoString(fi.fabric_attr.prov_name))
			// An error while converting a libfabric provider to a mercury provider is not fatal.
			// In this case, we want to omit this libfabric record from our results because it has no
			// mercury equivalent provider.  There are many providers in libfabric that have no mercury
			// equivalent, and we want to filter those out right here.
			continue
		}
		log.Debugf("Mercury provider list: %v", mercuryProviderList)

		scanResults := FabricScan{
			Provider:   mercuryProviderList,
			DeviceName: deviceAffinity.DeviceName,
			NUMANode:   deviceAffinity.NUMANode,
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

	if len(ScanResults) == 0 {
		log.Debugf("libfabric found records matching provider \"%s\" but there were no valid system devices that matched.", provider)
	}
	return ScanResults, nil
}
