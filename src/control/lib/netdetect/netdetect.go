//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package netdetect

/*
#cgo LDFLAGS: -lhwloc
#include <stdlib.h>
#include <hwloc.h>
#include <stdio.h>

#if HWLOC_API_VERSION >= 0x00020000

int cmpt_setFlags(hwloc_topology_t topology)
{
	return hwloc_topology_set_all_types_filter(topology, HWLOC_TYPE_FILTER_KEEP_ALL);
}

hwloc_obj_t cmpt_get_obj_by_depth(hwloc_topology_t topology, int depth, uint idx)
{
	return hwloc_get_obj_by_depth(topology, depth, idx);
}

uint cmpt_get_nbobjs_by_depth(hwloc_topology_t topology, int depth)
{
	return (uint)hwloc_get_nbobjs_by_depth(topology, depth);
}

int cmpt_get_parent_arity(hwloc_obj_t node)
{
	return node->parent->io_arity;
}

hwloc_obj_t cmpt_get_child(hwloc_obj_t node, int idx)
{
	hwloc_obj_t child;
	int i;

	child = node->parent->io_first_child;
	for (i = 0; i < idx; i++) {
		child = child->next_sibling;
	}
	return child;
}
#else
int cmpt_setFlags(hwloc_topology_t topology)
{
	return hwloc_topology_set_flags(topology, HWLOC_TOPOLOGY_FLAG_IO_DEVICES);
}

hwloc_obj_t cmpt_get_obj_by_depth(hwloc_topology_t topology, int depth, uint idx)
{
	return hwloc_get_obj_by_depth(topology, (uint)depth, idx);
}

uint cmpt_get_nbobjs_by_depth(hwloc_topology_t topology, int depth)
{
	return (uint)hwloc_get_nbobjs_by_depth(topology, (uint)depth);
}

int cmpt_get_parent_arity(hwloc_obj_t node)
{
	return node->parent->arity;
}

hwloc_obj_t cmpt_get_child(hwloc_obj_t node, int idx)
{
	return node->parent->children[idx];
}
#endif
*/
import "C"

import (
	"context"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
)

type key int

const (
	direct = iota
	sibling
	bestfit
	topologyKey key = 0
)

// alwaysExclude is a regexp to match always-excluded devices
var alwaysExclude = regexp.MustCompile(`(?:bond\d+)`)

// DeviceAffinity describes the essential details of a device and its NUMA affinity
type DeviceAffinity struct {
	DeviceName string
	CPUSet     string
	NodeSet    string
	NUMANode   uint
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

type hwlocProtectedAccess struct {
	mutex sync.Mutex
}

var hpa hwlocProtectedAccess

func (hpa *hwlocProtectedAccess) GetProcCPUBind(topology C.hwloc_topology_t, pid C.hwloc_pid_t, cpuset C.hwloc_cpuset_t, flags C.int) C.int {
	hpa.mutex.Lock()
	defer hpa.mutex.Unlock()
	status := C.hwloc_get_proc_cpubind(topology, pid, cpuset, flags)
	return status
}

func (hpa *hwlocProtectedAccess) topology_init() (C.hwloc_topology_t, error) {
	var topology C.hwloc_topology_t
	var status C.int

	defer func() {
		if status != 0 && topology != nil {
			cleanUp(topology)
		}
	}()

	hpa.mutex.Lock()
	defer hpa.mutex.Unlock()

	status = C.hwloc_topology_init(&topology)
	if status != 0 {
		return nil, errors.Errorf("hwloc_topology_init failure: %v", status)
	}

	status = C.cmpt_setFlags(topology)
	if status != 0 {
		return nil, errors.Errorf("hwloc setFlags failure: %v", status)
	}

	status = C.hwloc_topology_load(topology)
	if status != 0 {
		return nil, errors.Errorf("hwloc_topology_load failure: %v", status)
	}

	return topology, nil
}

type netdetectContext struct {
	topology      C.hwloc_topology_t
	numaAware     bool
	numNUMANodes  int
	coresPerNuma  int
	deviceScanCfg DeviceScan
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
	ndc.topology, err = initLib()
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
		cleanUp(ndc.topology)
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

// Cleanup releases the hwloc topology resources
func CleanUp(ctx context.Context) {
	ndc, err := getContext(ctx)
	if err != nil {
		return
	}
	cleanUp(ndc.topology)
}

// initLib initializes the hwloc library.
func initLib() (C.hwloc_topology_t, error) {
	version := C.hwloc_get_api_version()
	if (version >> 16) != (C.HWLOC_API_VERSION >> 16) {
		return nil, errors.Errorf("compilation error - compiled for hwloc API 0x%x but using library API 0x%x\n", C.HWLOC_API_VERSION, version)
	}

	topology, err := hpa.topology_init()
	if err != nil {
		return nil, err
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
		node := C.cmpt_get_obj_by_depth(deviceScanCfg.topology, C.int(deviceScanCfg.depth), C.uint(i))
		if node == nil {
			continue
		}
		hwlocDeviceNames = append(hwlocDeviceNames, C.GoString(node.name))
	}

	return hwlocDeviceNames, nil
}

// initDeviceScan initializes the hwloc library and initializes DeviceScan entries
// that are frequently used for hwloc queries.  If a valid context is provided,
// the topology associated with that context is used and hwloc will not be reinitialized.
// If the context is nil, or has a nil topology, hwloc is initialized.
func initDeviceScan(topology C.hwloc_topology_t) (DeviceScan, error) {
	var deviceScanCfg DeviceScan
	var err error

	deviceScanCfg.topology = topology

	depth := C.hwloc_get_type_depth(deviceScanCfg.topology, C.HWLOC_OBJ_OS_DEVICE)
	if depth != C.HWLOC_TYPE_DEPTH_OS_DEVICE {
		return deviceScanCfg,
			errors.New("hwloc_get_type_depth returned invalid value")
	}
	deviceScanCfg.depth = int(depth)
	deviceScanCfg.numObj = uint(C.cmpt_get_nbobjs_by_depth(deviceScanCfg.topology, C.int(depth)))

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
func getNodeDirect(deviceScanCfg DeviceScan) C.hwloc_obj_t {
	var i uint

	for i = 0; i < deviceScanCfg.numObj; i++ {
		node := C.cmpt_get_obj_by_depth(deviceScanCfg.topology, C.int(deviceScanCfg.depth), C.uint(i))
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
	var i C.int
	node := getNodeDirect(deviceScanCfg)
	if node == nil || node.parent == nil {
		return nil
	}
	// This node will have a sibling if its parent has more than one child (arity > 0)
	// Search the sibling node for one that has a name on the systemDeviceNamesMap
	// For example, hfi1_0 is a valid hwloc node.name, but it is not found on the systemDeviceNameMap.
	// The sibling of hfi1_0 is ib0, and ib0 *is* found on the systemDeviceNameMap.
	// The sibling device has the same non-I/O ancestor and shares the same NUMA Node, so we want that.

	count := C.cmpt_get_parent_arity(node)
	if count > 0 {
		for i = 0; i < count; i++ {
			child := C.cmpt_get_child(node, C.int(i))
			if _, found := deviceScanCfg.systemDeviceNamesMap[C.GoString(child.name)]; found {
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
func getNodeAlias(deviceScanCfg DeviceScan) C.hwloc_obj_t {
	var i C.int
	node := getNodeDirect(deviceScanCfg)
	if node == nil || node.parent == nil {
		return nil
	}

	// This node will have a sibling if its parent has more than one child (arity > 0)
	// Search for the first sibling node that has a different name than the search node name
	// and is not found on the systemDeviceNames map.
	count := C.cmpt_get_parent_arity(node)
	if count > 0 {
		for i = 0; i < count; i++ {
			child := C.cmpt_get_child(node, C.int(i))
			if _, found := deviceScanCfg.systemDeviceNamesMap[C.GoString(child.name)]; !found {
				return child
			}
		}
	}
	return nil
}

func getNodeSysFs(deviceScanCfg DeviceScan) C.hwloc_obj_t {
	log.Debugf("scanning sysfs for %q", deviceScanCfg.targetDevice)

	var hwlocObj C.hwloc_obj_t

	if err := filepath.Walk("/sys/devices", func(path string, fi os.FileInfo, err error) error {
		if fi == nil {
			return nil
		}
		if fi.Name() != deviceScanCfg.targetDevice {
			return nil
		}

		entries, err := ioutil.ReadDir(filepath.Join(path, "device", "net"))
		if err != nil {
			return nil
		}

		if len(entries) == 1 {
			deviceScanCfg.targetDevice = entries[0].Name()
			hwlocObj = getNodeDirect(deviceScanCfg)
			if hwlocObj != nil {
				return io.EOF
			}
		}

		return nil
	}); err == io.EOF {
		return hwlocObj
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

	numNuma := numNUMANodes(topology)
	if numNuma == 0 {
		log.Debugf("NUMA Node data is unavailable.  Using NUMA 0\n")
		return 0, nil
	}

	depth := C.hwloc_get_type_depth(topology, C.HWLOC_OBJ_NUMANODE)
	for i := 0; i < numNuma; i++ {
		numanode := C.cmpt_get_obj_by_depth(topology, C.int(depth), C.uint(i))
		if numanode == nil {
			// We don't want the lack of NUMA information to be an error.
			// If we get this far and can't access the NUMA topology data,
			// we will use NUMA ID 0.
			log.Debugf("NUMA Node data is unavailable.  Using NUMA 0\n")
			return 0, nil
		}

		if C.hwloc_bitmap_isincluded(ancestorNode.cpuset, numanode.cpuset) != 0 {
			return uint(numanode.logical_index), nil
		}
	}

	log.Debugf("Unable to determine NUMA socket ID.  Using NUMA 0")
	return 0, nil
}

func numNUMANodes(topology C.hwloc_topology_t) int {
	depth := C.hwloc_get_type_depth(topology, C.HWLOC_OBJ_NUMANODE)
	numObj := int(C.cmpt_get_nbobjs_by_depth(topology, C.int(depth)))
	return numObj
}

func getCoreCount(topology C.hwloc_topology_t) (int, error) {
	depth := C.hwloc_get_type_depth(topology, C.HWLOC_OBJ_CORE)
	if depth == C.HWLOC_TYPE_DEPTH_UNKNOWN {
		return 0, errors.New("number of cpu cores could not be detected")
	}

	return int(C.cmpt_get_nbobjs_by_depth(topology, C.int(depth))), nil
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

	cpuset := C.hwloc_bitmap_alloc()
	defer C.hwloc_bitmap_free(cpuset)

	status := hpa.GetProcCPUBind(ndc.topology, C.int(pid), cpuset, 0)
	if status != 0 {
		return 0, errors.Errorf("NUMA Node data is unavailable.")
	}

	nodeset := C.hwloc_bitmap_alloc()
	defer C.hwloc_bitmap_free(nodeset)
	C.hwloc_cpuset_to_nodeset(ndc.topology, cpuset, nodeset)

	depth := C.hwloc_get_type_depth(ndc.topology, C.HWLOC_OBJ_NUMANODE)
	for i := 0; i < ndc.numNUMANodes; i++ {
		numanode := C.cmpt_get_obj_by_depth(ndc.topology, C.int(depth), C.uint(i))
		if numanode == nil {
			return 0, errors.Errorf("NUMA Node data is unavailable.")
		}

		if C.hwloc_bitmap_intersects(nodeset, numanode.nodeset) != 0 {
			return int(numanode.logical_index), nil
		}

		if C.hwloc_bitmap_intersects(cpuset, numanode.cpuset) != 0 {
			return int(numanode.logical_index), nil
		}
	}

	return 0, errors.Errorf("NUMA Node data is unavailable.")
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
	var node C.hwloc_obj_t
	var cpuset *C.char
	var nodeset *C.char

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

	switch getLookupMethod(deviceScanCfg) {
	case direct:
		node = getNodeDirect(deviceScanCfg)
	case sibling:
		node = getNodeSibling(deviceScanCfg)
	case bestfit:
		node = getNodeBestFit(deviceScanCfg)
	default:
		return DeviceAffinity{}, errors.Errorf("unsupported lookup method")
	}
	// as a last resort try to figure it out directly from sysfs, but
	// this can be slow...
	if node == nil {
		node = getNodeSysFs(deviceScanCfg)
	}

	// At this point, we know the topology is NUMA aware.
	// Returning a default device affinity of NUMA 0 would no longer be reasonable.
	if node == nil {
		return DeviceAffinity{}, errors.Errorf("cannot determine device affinity because the device was not found in the topology: %s", deviceScanCfg.targetDevice)
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

// mercuryToLibFabric converts a single Mercury fabric provider string into a libfabric compatible provider string
func mercuryToLibFabric(provider string) string {
	switch provider {
	case "ofi+sockets":
		return "sockets"
	case "ofi+tcp":
		return "tcp"
	case "ofi+verbs":
		return "verbs"
	case "ofi_rxm":
		return "ofi_rxm"
	case "ofi+psm2":
		return "psm2"
	case "ofi+gni":
		return "gni"
	case "ofi+cxi":
		return "cxi"
	default:
		return provider
	}
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
		libFabricProviderList += mercuryToLibFabric(subProvider) + ";"
	}
	return strings.TrimSuffix(libFabricProviderList, ";"), nil
}

// libFabricToMercury converts a single libfabric provider string into a Mercury compatible provider string
func libFabricToMercury(provider string) string {
	switch provider {
	case "sockets":
		return "ofi+sockets"
	case "tcp":
		return "ofi+tcp"
	case "verbs":
		return "ofi+verbs"
	case "ofi_rxm":
		return "ofi_rxm"
	case "psm2":
		return "ofi+psm2"
	case "gni":
		return "ofi+gni"
	case "cxi":
		return "ofi+cxi"
	default:
		return provider
	}
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
		mercuryProviderList += libFabricToMercury(subProvider) + ";"
	}

	// Success if we converted at least one provider
	if mercuryProviderList != "" {
		return strings.TrimSuffix(mercuryProviderList, ";"), nil
	}

	return "", errors.Errorf("failed to convert the libfabric provider list '%s' to any mercury provider", provider)
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
