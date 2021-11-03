//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package netdetect

/*
#cgo LDFLAGS: -lhwloc -lfabric
#include <stdlib.h>
#include <stdio.h>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>
#include <rdma/fi_rma.h>
#include <rdma/fi_errno.h>

#define getHFIUnitError -2
typedef struct {
	uint64_t reserved_1;
	uint8_t  reserved_2;
	int8_t   unit;
	uint8_t  port;
	uint8_t  reserved_3;
	uint32_t service;
} psmx2_ep_name;

int getHFIUnit(void *src_addr) {
	psmx2_ep_name *psmx2;
	psmx2 = (psmx2_ep_name *)src_addr;
	if (!psmx2)
		return getHFIUnitError;
	return psmx2->unit;
}
*/
import "C"

import (
	"context"
	"fmt"
	"io/ioutil"
	"net"
	"strconv"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/netdetect/hwloc"
	"github.com/daos-stack/daos/src/control/logging"
)

type key int

const (
	direct = iota
	sibling
	bestfit
	libFabricMajorVersion     = 1
	libFabricMinorVersion     = 7
	allHFIUsed                = -1
	badAddress                = C.getHFIUnitError
	topologyKey           key = 0
	// ARP protocol hardware identifiers: https://elixir.free-electrons.com/linux/v4.0/source/include/uapi/linux/if_arp.h#L29
	Netrom     = 0
	Ether      = 1
	Eether     = 2
	Ax25       = 3
	Pronet     = 4
	Chaos      = 5
	IEEE802    = 6
	Arcnet     = 7
	Appletlk   = 8
	Dlci       = 15
	Atm        = 19
	Metricom   = 23
	IEEE1394   = 24
	Eui64      = 27
	Infiniband = 32
	Loopback   = 772
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
	Provider    string `json:"provider"`
	DeviceName  string `json:"device"`
	NUMANode    uint   `json:"numanode"`
	Priority    int    `json:"priority"`
	NetDevClass uint32 `json:"netdevclass"`
}

func (fs *FabricScan) String() string {
	return fmt.Sprintf("\tfabric_iface: %v\n\tprovider: %v\n\tpinned_numa_node: %d", fs.DeviceName, fs.Provider, fs.NUMANode)
}

// DeviceScan caches initialization data for later hwloc usage
type DeviceScan struct {
	topology             hwloc.Topology
	depth                int
	numObj               uint
	systemDeviceNames    []string
	systemDeviceNamesMap map[string]struct{}
	hwlocDeviceNames     []string
	hwlocDeviceNamesMap  map[string]struct{}
	targetDevice         string
}

var log logging.Logger = logging.NewStdoutLogger("netdetect")

// SetLogger sets the package-level logger
func SetLogger(l logging.Logger) {
	log = l
}

func (da *DeviceAffinity) String() string {
	return fmt.Sprintf("%s:%s:%s:%d", da.DeviceName, da.CPUSet, da.NodeSet, da.NUMANode)
}

type netdetectContext struct {
	api             hwloc.API
	topology        hwloc.Topology
	cleanupTopology func()
	numaAware       bool
	numNUMANodes    uint
	coresPerNuma    uint
	deviceScanCfg   DeviceScan
}

func getContext(ctx context.Context) (*netdetectContext, error) {
	if ctx == nil {
		return nil, errors.Errorf("invalid input context")
	}

	ndc, ok := ctx.Value(topologyKey).(netdetectContext)
	if !ok || ndc.topology == nil {
		return nil, errors.Errorf("context not initialized")
	}
	return &ndc, nil
}

func Init(parent context.Context) (context.Context, error) {
	var err error

	if parent == nil {
		parent = context.Background()
	}

	ndc := netdetectContext{}
	ndc.api = hwloc.GetAPI()
	ndc.topology, ndc.cleanupTopology, err = hwloc.GetTopology(ndc.api)
	if err != nil {
		return nil, errors.Errorf("unable to initialize netdetect context: %v", err)
	}
	ndc.numNUMANodes = numNUMANodes(ndc.topology)
	ndc.numaAware = ndc.numNUMANodes > 0
	if ndc.numaAware {
		cores, err := getCoreCount(ndc.topology)
		if err != nil {
			return nil, err
		}
		ndc.coresPerNuma = cores / ndc.numNUMANodes
		log.Debugf("%d NUMA nodes detected with %d cores per node",
			ndc.numNUMANodes, ndc.coresPerNuma)
	}

	ndc.deviceScanCfg, err = initDeviceScan(ndc.topology)
	log.Debugf("network detection, system names: %v, hwloc names %v",
		ndc.deviceScanCfg.systemDeviceNames, ndc.deviceScanCfg.hwlocDeviceNames)
	if err != nil {
		ndc.cleanupTopology()
		return nil, err
	}

	return context.WithValue(parent, topologyKey, ndc), nil
}

// HasNUMA returns true if the topology has NUMA node data
func HasNUMA(ctx context.Context) bool {
	ndc, err := getContext(ctx)
	if err != nil {
		return false
	}
	return ndc.numaAware
}

// NumNumaNodes returns the number of detected NUMA nodes, or 0 if none.
func NumNumaNodes(ctx context.Context) uint {
	ndc, err := getContext(ctx)
	if err != nil || !HasNUMA(ctx) {
		return 0
	}
	return ndc.numNUMANodes
}

// CoresPerNuma returns the number of cores on each NUMA node, or 0 if not NUMA
// aware.
func CoresPerNuma(ctx context.Context) uint {
	ndc, err := getContext(ctx)
	if err != nil || !HasNUMA(ctx) {
		return 0
	}
	return ndc.coresPerNuma
}

// Cleanup releases the hwloc topology resources
func CleanUp(ctx context.Context) {
	ndc, err := getContext(ctx)
	if err != nil {
		return
	}
	ndc.cleanupTopology()
}

// getHwlocDeviceNames retrieves all of the system device names that hwloc knows about
func getHwlocDeviceNames(deviceScanCfg DeviceScan) ([]string, error) {
	var hwlocDeviceNames []string
	var i uint

	if deviceScanCfg.topology == nil {
		return hwlocDeviceNames, errors.New("hwloc was not initialized")
	}

	for i = 0; i < deviceScanCfg.numObj; i++ {
		node, err := deviceScanCfg.topology.GetObjByDepth(deviceScanCfg.depth, i)
		if err != nil {
			continue
		}
		hwlocDeviceNames = append(hwlocDeviceNames, node.Name())
	}

	return hwlocDeviceNames, nil
}

// initDeviceScan initializes the hwloc library and initializes DeviceScan entries
// that are frequently used for hwloc queries.  If a valid context is provided,
// the topology associated with that context is used and hwloc will not be reinitialized.
// If the context is nil, or has a nil topology, hwloc is initialized.
func initDeviceScan(topology hwloc.Topology) (DeviceScan, error) {
	var deviceScanCfg DeviceScan
	var err error

	deviceScanCfg.topology = topology

	depth := deviceScanCfg.topology.GetTypeDepth(hwloc.ObjTypeOSDevice)
	if depth != hwloc.TypeDepthOSDevice {
		return deviceScanCfg,
			errors.New("hwloc_get_type_depth returned invalid value")
	}
	deviceScanCfg.depth = depth
	deviceScanCfg.numObj = deviceScanCfg.topology.GetNumObjAtDepth(depth)

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
func getNodeDirect(deviceScanCfg DeviceScan) hwloc.Object {
	var i uint

	for i = 0; i < deviceScanCfg.numObj; i++ {
		node, err := deviceScanCfg.topology.GetObjByDepth(deviceScanCfg.depth, i)
		if err != nil {
			continue
		}

		if node.Name() == deviceScanCfg.targetDevice {
			return node
		}
	}
	return nil
}

// getNodeSibling finds a node object that is the sibling of the device being matched.  The sibling will be something found
// on the systemDevicesNameMap.
func getNodeSibling(deviceScanCfg DeviceScan) hwloc.Object {
	node := getNodeDirect(deviceScanCfg)
	if node == nil {
		return nil
	}
	// This node will have a sibling if its parent has more than one child (arity > 0)
	// Search the sibling node for one that has a name on the systemDeviceNamesMap
	// For example, hfi1_0 is a valid hwloc node.name, but it is not found on the systemDeviceNameMap.
	// The sibling of hfi1_0 is ib0, and ib0 *is* found on the systemDeviceNameMap.
	// The sibling device has the same non-I/O ancestor and shares the same NUMA Node, so we want that.

	count := node.GetNumSiblings()
	if count > 0 {
		for i := uint(0); i < count; i++ {
			child, err := node.GetChild(i)
			if err != nil {
				continue
			}
			if _, found := deviceScanCfg.systemDeviceNamesMap[child.Name()]; found {
				return child
			}
		}
	}
	return nil
}

// getNodeAlias finds a node object that is the sibling of the device being matched.
// This function is now tuned to search in one direction, from a system device to a sibling
// that is not also a system device.  This allows differentiating between multiple devices
// "ib0, ib1 ..." and the interface adapter they are connected to "hfi1_0, mlx4_0 ..."
func getNodeAlias(deviceScanCfg DeviceScan) hwloc.Object {
	node := getNodeDirect(deviceScanCfg)
	if node == nil {
		return nil
	}

	// This node will have a sibling if its parent has more than one child (arity > 0)
	// Search for the first sibling node that has a different name than the search node name
	// and is not found on the systemDeviceNames map.
	count := node.GetNumSiblings()
	if count > 0 {
		for i := uint(0); i < count; i++ {
			child, err := node.GetChild(i)
			if err != nil {
				continue
			}
			if _, found := deviceScanCfg.systemDeviceNamesMap[child.Name()]; !found {
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
func getNodeBestFit(deviceScanCfg DeviceScan) hwloc.Object {
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
func getNUMASocketID(topology hwloc.Topology, node hwloc.Object) (uint, error) {
	if node == nil {
		return 0, errors.New("invalid node provided")
	}

	if node.Name() == "lo" {
		return 0, nil
	}

	numanode, err := node.GetAncestorByType(hwloc.ObjTypeNUMANode)
	if err == nil {
		return numanode.LogicalIndex(), nil
	}

	// If we made it here, it means that we're running in some flavor of a restricted environment
	// which caused the numa node ancestor lookup to fail.  We're not restricted from looking at the
	// cpuset for each numa node, so we can look for an intersection of the node's cpuset with each
	// numa node cpuset until a match is found or we run out of candidates.
	ancestorNode, err := node.GetNonIOAncestor()
	if err != nil {
		return 0, err
	}

	numNuma := numNUMANodes(topology)
	if numNuma == 0 {
		log.Debugf("NUMA Node data is unavailable.  Using NUMA 0\n")
		return 0, nil
	}

	log.Debugf("There are %d NUMA nodes.", numNuma)

	depth := topology.GetTypeDepth(hwloc.ObjTypeNUMANode)
	for i := uint(0); i < numNuma; i++ {
		numanode, err := topology.GetObjByDepth(depth, i)
		if err != nil {
			// We don't want the lack of NUMA information to be an error.
			// If we get this far and can't access the NUMA topology data,
			// we will use NUMA ID 0.
			log.Debugf("NUMA Node data is unavailable.  Using NUMA 0\n")
			return 0, nil
		}

		if ancestorNode.CPUSet().IsSubsetOf(numanode.CPUSet()) {
			return numanode.LogicalIndex(), nil
		}
	}

	log.Debugf("Unable to determine NUMA socket ID.  Using NUMA 0")
	return 0, nil
}

func numNUMANodes(topology hwloc.Topology) uint {
	depth := topology.GetTypeDepth(hwloc.ObjTypeNUMANode)
	return topology.GetNumObjAtDepth(depth)
}

func getCoreCount(topology hwloc.Topology) (uint, error) {
	depth := topology.GetTypeDepth(hwloc.ObjTypeCore)
	if depth == hwloc.TypeDepthUnknown {
		return 0, errors.New("number of cpu cores could not be detected")
	}

	return topology.GetNumObjAtDepth(depth), nil
}

// GetNUMASocketIDForPid determines the cpuset and nodeset corresponding to the given pid.
// It looks for an intersection between the nodeset or cpuset of this pid and the nodeset or cpuset of each
// NUMA node looking for a match to identify the corresponding NUMA socket ID.
func GetNUMASocketIDForPid(ctx context.Context, pid int32) (int, error) {
	ndc, err := getContext(ctx)
	if err != nil {
		return 0, errors.Errorf("netdetect context was not initialized")
	}

	if !ndc.numaAware {
		return 0, errors.Errorf("NUMA Node data is unavailable.")
	}

	cpuSet, cleanupCPUSet, err := ndc.topology.GetProcCPUBind(pid, 0)
	if err != nil {
		return 0, err
	}
	defer cleanupCPUSet()

	nodeset, cleanupNodeSet, err := cpuSet.ToNodeSet()
	if err != nil {
		return 0, err
	}
	defer cleanupNodeSet()

	depth := ndc.topology.GetTypeDepth(hwloc.ObjTypeNUMANode)
	for i := uint(0); i < ndc.numNUMANodes; i++ {
		numanode, err := ndc.topology.GetObjByDepth(depth, i)
		if err != nil {
			return 0, errors.Errorf("NUMA Node data is unavailable.")
		}

		if nodeset.Intersects(numanode.NodeSet()) {
			return int(numanode.LogicalIndex()), nil
		}

		if cpuSet.Intersects(numanode.CPUSet()) {
			return int(numanode.LogicalIndex()), nil
		}
	}

	return 0, errors.Errorf("NUMA Node data is unavailable.")
}

// GetDeviceAlias is a wrapper for getDeviceAliasWithSystemList.  This interface
// specifies an empty additionalSystemDevices list which allows for the default behavior we want
// for normal use.  Test functions can call getDeviceAliasWithSystemList() directly and specify
// an arbitrary additionalSystemDevice list as necessary.
func GetDeviceAlias(ctx context.Context, device string) (string, error) {
	additionalSystemDevices := []string{}
	return getDeviceAliasWithSystemList(ctx, device, additionalSystemDevices)
}

// getDeviceAliasWithSystemList finds an alias for the device name provided.
// For example, the device alias for "ib0" is a sibling node in the hwloc topology
// with the name "hfi1_0".  Any devices specified by additionalSystemDevices are added
// to the system device list that is automatically determined.
func getDeviceAliasWithSystemList(ctx context.Context, device string, additionalSystemDevices []string) (string, error) {
	log.Debugf("Searching for a device alias for: %s", device)

	ndc, err := getContext(ctx)
	if err != nil {
		return "", errors.Errorf("hwloc topology not initialized")
	}

	ndc.deviceScanCfg.targetDevice = device

	// The loopback device isn't a physical device that hwloc will find in the topology
	// If "lo" is specified, it is treated specially.
	if ndc.deviceScanCfg.targetDevice == "lo" {
		return "lo", nil
	}

	// Add any additional system devices to the map
	for _, deviceName := range additionalSystemDevices {
		ndc.deviceScanCfg.systemDeviceNamesMap[deviceName] = struct{}{}
	}

	node := getNodeAlias(ndc.deviceScanCfg)
	if node == nil {
		return "", errors.Errorf("unable to find an alias for: %s", ndc.deviceScanCfg.targetDevice)
	}
	log.Debugf("Device alias for %s is %s", device, node.Name())
	return node.Name(), nil
}

// GetAffinityForDevice searches the system topology reported by hwloc
// for the device specified by netDeviceName and returns the corresponding
// name, cpuset, nodeset, and NUMA node ID information.
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
	if deviceScanCfg.topology == nil {
		return DeviceAffinity{}, errors.New("hwloc library not yet initialized")
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

	// If the system isn't NUMA aware, use numa 0
	if numNUMANodes(deviceScanCfg.topology) == 0 {
		return DeviceAffinity{
			DeviceName: deviceScanCfg.targetDevice,
			CPUSet:     "0x0",
			NodeSet:    "0x1",
			NUMANode:   0,
		}, nil
	}

	var node hwloc.Object
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

	// At this point, we know the topology is NUMA aware.
	// Returning a default device affinity of NUMA 0 would no longer be reasonable.
	if node == nil {
		return DeviceAffinity{}, errors.Errorf("cannot determine device affinity because the device was not found in the topology: %s", deviceScanCfg.targetDevice)
	}

	ancestorNode, err := node.GetNonIOAncestor()
	if err != nil {
		return DeviceAffinity{}, errors.Errorf("unable to find an ancestor node in topology for device for device: %s", deviceScanCfg.targetDevice)
	}

	cpuset := ancestorNode.CPUSet().String()
	nodeset := ancestorNode.NodeSet().String()

	numaSocket, err := getNUMASocketID(deviceScanCfg.topology, node)
	if err != nil {
		return DeviceAffinity{}, err
	}

	return DeviceAffinity{
		DeviceName: node.Name(),
		CPUSet:     cpuset,
		NodeSet:    nodeset,
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

// GetSupportedProviders returns a []string containing all supported Mercury providers
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
	return "", errors.Errorf("unknown fabric provider %q", provider)
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
			return "", errors.Errorf("unknown fabric provider %q", subProvider)
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

	return "", errors.Errorf("unknown fabric provider %q", provider)
}

// convertLibFabricToMercury converts a libfabric provider string containing one or more providers
// separated by ';' into a Mercury compatible provider string
// At least one provider in the list must convert to a Mercury provider in order to have success.
func convertLibFabricToMercury(provider string) (string, error) {
	var mercuryProviderList string

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
func ValidateProviderStub(ctx context.Context, device string, provider string) error {
	// Call the full function to get the results without generating any hard errors
	err := ValidateProviderConfig(ctx, device, provider)
	if err != nil {
		log.Debugf("ValidateProviderConfig (device: %s, provider %s) returned error: %v", device, provider, err)
	}
	return nil
}

// deviceProviderMatch is a helper function to consolidate this functionality used by the provider validation
func deviceProviderMatch(deviceScanCfg DeviceScan, provider string, systemDevice string) bool {
	deviceAffinity, err := GetAffinityForDevice(deviceScanCfg)
	if err != nil {
		return false
	}
	if deviceAffinity.DeviceName == systemDevice {
		log.Debugf("Device %s supports provider: %s", systemDevice, provider)
		return true
	}
	return false
}

func getHFIDeviceCount(hwlocDeviceNames []string) int {
	var hfiDeviceCount int
	for _, device := range hwlocDeviceNames {
		if strings.Contains(device, "hfi1_") {
			hfiDeviceCount++
		}
	}
	return hfiDeviceCount
}

// ValidateProviderConfig confirms that the given network device supports the chosen provider
func ValidateProviderConfig(ctx context.Context, device string, provider string) error {
	var fi *C.struct_fi_info
	var hints *C.struct_fi_info

	if provider == "" {
		return errors.New("provider required")
	}

	// Allow provider 'sm' (shared memory) to pass validation without passing any of the checks
	// The provider is valid to use, but does not use devices.  Therefore, it is handled specially to avoid
	// flagging it as an error.
	if provider == "sm" {
		return nil
	}

	if device == "" {
		return errors.New("device required")
	}

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

	ndc, err := getContext(ctx)
	if err != nil {
		return errors.New("hwloc topology not initialized")
	}

	if _, found := ndc.deviceScanCfg.systemDeviceNamesMap[device]; !found {
		return errors.Errorf("device: %s is an invalid device name", device)
	}

	hfiDeviceCount := getHFIDeviceCount(ndc.deviceScanCfg.hwlocDeviceNames)

	// iterate over the libfabric records that match this provider
	// and look for one that has a matching device name
	// The device names returned from libfabric may be device names such as "hfi1_0" that maps to a system device "ib0"
	// or may be a decorated device name such as "hfi1_0-dgram" that maps to "hfi1_0" that maps to a system device "ib0"
	// In order to find a device and provider match, the libfabric device name must be converted into a system device name.
	// GetAffinityForDevice() is used to provide this conversion inside deviceProviderMatch()
	for ; fi != nil; fi = fi.next {
		if fi.domain_attr == nil || fi.domain_attr.name == nil || fi.fabric_attr == nil || fi.fabric_attr.prov_name == nil {
			continue
		}
		ndc.deviceScanCfg.targetDevice = C.GoString(fi.domain_attr.name)

		// Implements a workaround to handle the current psm2 provider behavior
		// that reports fi.domain_attr.name as "psm2" instead of an actual device
		// name like "hfi1_0".
		//
		// The work around behaves in two different ways depending on the data available.
		// When the value of fi.src_addr.unit is 0 or greater, it is used to explicitly
		// define an hfi1_ unit by appending that number to the "hfi1_" string.
		//
		// When the value of fi.src_addr.unit is -1, it means that all hfi1_x units in the system
		// are supported by the psm2 provider.  In this case, we need to know how many hfi1_x units
		// there are, and can look for a provider/device match against each item in the list.
		//
		// Note that "hfi1_x" is converted to a system device name such as "ibx"
		// by the call to GetAffinityForDevice() as part of deviceProviderMatch()
		if C.GoString(fi.fabric_attr.prov_name) == "psm2" {
			if strings.Contains(ndc.deviceScanCfg.targetDevice, "psm2") {
				log.Debugf("psm2 provider and psm2 device found.")
				hfiUnit := C.getHFIUnit(fi.src_addr)
				switch hfiUnit {
				case allHFIUsed:
					for deviceID := 0; deviceID < hfiDeviceCount; deviceID++ {
						ndc.deviceScanCfg.targetDevice = fmt.Sprintf("hfi1_%d", deviceID)
						if deviceProviderMatch(ndc.deviceScanCfg, provider, device) {
							return nil
						}
					}
					continue
				case badAddress:
					log.Debugf("The fi.src_addr was null - not adding hfi1_n scan result")
					continue
				default: // The hfi unit is stated explicitly
					log.Debugf("Found hfi1_%d (actual)", hfiUnit)
					ndc.deviceScanCfg.targetDevice = fmt.Sprintf("hfi1_%d", hfiUnit)
					if deviceProviderMatch(ndc.deviceScanCfg, provider, device) {
						return nil
					}
					continue
				}
			}
		}

		if deviceProviderMatch(ndc.deviceScanCfg, provider, device) {
			return nil
		}
	}
	return errors.Errorf("Device %s does not support provider: %s", device, provider)
}

// ValidateNUMAStub is used for most unit testing to replace ValidateNUMAConfig because the network configuration
// validation depends upon physical hardware resources and configuration on the target machine
// that are either not known or static in the test environment
func ValidateNUMAStub(ctx context.Context, device string, numaNode uint) error {

	err := ValidateNUMAConfig(ctx, device, numaNode)
	if err != nil {
		log.Debugf("ValidateNUMAConfig (device: %s, NUMA: %d) returned error: %v", device, numaNode, err)
	}
	return nil
}

// ValidateNUMAConfig confirms that the given network device matches the NUMA ID given.
func ValidateNUMAConfig(ctx context.Context, device string, numaNode uint) error {
	var err error

	if device == "" {
		return errors.New("device required")
	}

	ndc, err := getContext(ctx)
	if err != nil {
		return errors.New("hwloc topology not initialized")
	}

	// If the system isn't NUMA aware, skip validation
	if !ndc.numaAware {
		log.Debugf("The system is not NUMA aware.  Device/NUMA validation skipped.\n")
		return nil
	}

	if _, found := ndc.deviceScanCfg.systemDeviceNamesMap[device]; !found {
		return errors.Errorf("device: %s is an invalid device name", device)
	}

	ndc.deviceScanCfg.targetDevice = device
	deviceAffinity, err := GetAffinityForDevice(ndc.deviceScanCfg)
	if err != nil {
		return err
	}

	if deviceAffinity.NUMANode != numaNode {
		return errors.Errorf("The NUMA node for device %s does not match the provided value %d.", device, numaNode)
	}
	return nil
}

func createFabricScanEntry(deviceScanCfg DeviceScan, provider string, devCount int, resultsMap map[string]struct{}, excludeMap map[string]struct{}) (*FabricScan, error) {
	deviceAffinity, err := GetAffinityForDevice(deviceScanCfg)
	if err != nil {
		return nil, err
	}

	if deviceScanCfg.targetDevice != deviceAffinity.DeviceName {
		log.Debugf("The target device name has been converted to system device name: %s", deviceAffinity.DeviceName)
	}

	// Convert the libfabric provider list to a Mercury compatible provider list
	mercuryProviderList, err := convertLibFabricToMercury(provider)
	if err != nil {
		// An error while converting a libfabric provider to a mercury provider is not fatal.
		// In this case, we want to omit this libfabric record from our results because it has no
		// mercury equivalent provider.  There are many providers in libfabric that have no mercury
		// equivalent, and we want to filter those out right here.
		return nil, err
	}

	devClass, err := GetDeviceClass(deviceAffinity.DeviceName)
	if err != nil {
		return nil, err
	}

	scanResults := &FabricScan{
		Provider:    mercuryProviderList,
		DeviceName:  deviceAffinity.DeviceName,
		NUMANode:    deviceAffinity.NUMANode,
		Priority:    devCount,
		NetDevClass: devClass,
	}

	if _, skip := excludeMap[scanResults.DeviceName]; skip {
		return nil, errors.New("excluded device entry")
	}

	results := scanResults.String()

	if _, found := resultsMap[results]; found {
		return nil, errors.New("duplicate entry")
	}

	resultsMap[results] = struct{}{}
	log.Debugf("\n%s", results)
	return scanResults, nil
}

// ScanFabric examines libfabric data to find the network devices that support the given fabric provider.
func ScanFabric(ctx context.Context, provider string, excludes ...string) ([]*FabricScan, error) {
	var ScanResults []*FabricScan
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

	ndc, err := getContext(ctx)
	if err != nil {
		return ScanResults, nil
	}

	hfiDeviceCount := getHFIDeviceCount(ndc.deviceScanCfg.hwlocDeviceNames)
	log.Debugf("There are %d hfi1 devices in the system", hfiDeviceCount)

	excludeMap := make(map[string]struct{})
	for _, iface := range excludes {
		excludeMap[iface] = struct{}{}
	}

	for ; fi != nil; fi = fi.next {
		if fi.domain_attr == nil || fi.domain_attr.name == nil || fi.fabric_attr == nil || fi.fabric_attr.prov_name == nil {
			continue
		}
		ndc.deviceScanCfg.targetDevice = C.GoString(fi.domain_attr.name)
		// Implements a workaround to handle the current psm2 provider behavior
		// that reports fi.domain_attr.name as "psm2" instead of an actual device
		// name like "hfi1_0".
		//
		// The work around behaves in two different ways depending on the data available.
		// When the value of fi.src_addr.unit is 0 or greater, it is used to explicitly
		// define an hfi1_ unit by appending that number to the "hfi1_" string.
		//
		// When the value of fi.src_addr.unit is -1, it means that all hfi1_x units in the system
		// are supported by the psm2 provider.  In this case, we need to know how many hfi1_x units
		// there are, and create a fabric scan entry for each one.  By querying the device names from
		// hwloc, we can count the number of devices matching "hfi1_" and create a device scan
		// entry for each one.
		if provider == "" {
			provider = "All"
		}

		if strings.Contains(C.GoString(fi.fabric_attr.prov_name), "psm2") {
			if strings.Contains(ndc.deviceScanCfg.targetDevice, "psm2") {
				log.Debugf("psm2 provider and psm2 device found.")
				hfiUnit := C.getHFIUnit(fi.src_addr)
				switch hfiUnit {
				case allHFIUsed:
					for deviceID := 0; deviceID < hfiDeviceCount; deviceID++ {
						ndc.deviceScanCfg.targetDevice = fmt.Sprintf("hfi1_%d", deviceID)
						devScanResults, err := createFabricScanEntry(ndc.deviceScanCfg, C.GoString(fi.fabric_attr.prov_name), devCount, resultsMap, excludeMap)
						if err != nil {
							continue
						}
						log.Debugf("scan result: %v\n", devScanResults)
						ScanResults = append(ScanResults, devScanResults)
						devCount++
					}
					continue
				case badAddress:
					log.Debugf("The fi.src_addr was null - not adding hfi1_n scan result")
					continue
				default: // The hfi unit is stated explicitly
					log.Debugf("Found hfi1_%d (actual)", hfiUnit)
					ndc.deviceScanCfg.targetDevice = fmt.Sprintf("hfi1_%d", hfiUnit)
					continue
				}
			}
		}

		devScanResults, err := createFabricScanEntry(ndc.deviceScanCfg, C.GoString(fi.fabric_attr.prov_name), devCount, resultsMap, excludeMap)
		if err != nil {
			continue
		}
		ScanResults = append(ScanResults, devScanResults)
		devCount++
	}

	if len(ScanResults) == 0 {
		log.Debugf("libfabric found records matching provider \"%s\" but there were no valid system devices that matched.", provider)
	}
	return ScanResults, nil
}

// GetDeviceClass determines the device type according to what's stored in the filesystem
// Returns an integer value corresponding to its ARP protocol hardware identifier
// found here: https://elixir.free-electrons.com/linux/v4.0/source/include/uapi/linux/if_arp.h#L29
func GetDeviceClass(netdev string) (uint32, error) {
	devClass, err := ioutil.ReadFile(fmt.Sprintf("/sys/class/net/%s/type", netdev))
	if err != nil {
		return 0, err
	}

	res, err := strconv.Atoi(strings.TrimSpace(string(devClass)))
	return uint32(res), err
}

// Convert a network device class ID to a string identifier
func DevClassName(class uint32) string {
	switch class {
	case Netrom:
		return "NETROM"
	case Ether:
		return "ETHER"
	case Eether:
		return "EETHER"
	case Ax25:
		return "AX25"
	case Pronet:
		return "PRONET"
	case Chaos:
		return "CHAOS"
	case IEEE802:
		return "IEEE802"
	case Arcnet:
		return "ARCNET"
	case Appletlk:
		return "APPLETLK"
	case Dlci:
		return "DLCI"
	case Atm:
		return "ATM"
	case Metricom:
		return "METRICOM"
	case IEEE1394:
		return "IEEE1394"
	case Eui64:
		return "EUI64"
	case Infiniband:
		return "INFINIBAND"
	case Loopback:
		return "LOOPBACK"
	default:
		return "UNKNOWN"
	}
}
