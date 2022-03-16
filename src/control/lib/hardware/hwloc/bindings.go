//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hwloc

/*
#cgo LDFLAGS: -lhwloc
#include <hwloc.h>

struct bridge_downstream {
	unsigned short domain;
	unsigned char secondary_bus;
	unsigned char subordinate_bus;
};

struct bridge_downstream node_get_bridge_downstream(hwloc_obj_t node)
{
	struct bridge_downstream result;
	result.domain = node->attr->bridge.downstream.pci.domain;
	result.secondary_bus = node->attr->bridge.downstream.pci.secondary_bus;
	result.subordinate_bus = node->attr->bridge.downstream.pci.subordinate_bus;
	return result;
}

int node_get_bridge_type(hwloc_obj_t node)
{
	if (node->attr == NULL) {
		return -1;
	}

	return node->attr->bridge.upstream_type;
}

int node_get_osdev_type(hwloc_obj_t node)
{
	if (node->attr == NULL) {
		return -1;
	}

	return node->attr->osdev.type;
}

struct hwloc_pcidev_attr_s *node_get_pcidev_attr(hwloc_obj_t node) {
	if (node->attr == NULL) {
		return NULL;
	}

	return &node->attr->pcidev;
}

#if HWLOC_API_VERSION >= 0x00020000

int topo_setFlags(hwloc_topology_t topology)
{
	return hwloc_topology_set_all_types_filter(topology, HWLOC_TYPE_FILTER_KEEP_ALL);
}

int node_get_arity(hwloc_obj_t node)
{
	return node->io_arity;
}

int node_get_parent_arity(hwloc_obj_t node)
{
	return node->parent->io_arity;
}

hwloc_obj_t node_get_sibling(hwloc_obj_t node, int idx)
{
	hwloc_obj_t child;
	int i;

	child = node->parent->io_first_child;
	for (i = 0; i < idx; i++) {
		child = child->next_sibling;
	}
	return child;
}

hwloc_obj_t node_get_child(hwloc_obj_t node, int idx)
{
	hwloc_obj_t child;
	int i;

	child = node->io_first_child;
	for (i = 0; i < idx; i++) {
		child = child->next_sibling;
	}
	return child;
}
#else
int topo_setFlags(hwloc_topology_t topology)
{
	return hwloc_topology_set_flags(topology, HWLOC_TOPOLOGY_FLAG_WHOLE_IO);
}

int node_get_arity(hwloc_obj_t node)
{
	return node->arity;
}

int node_get_parent_arity(hwloc_obj_t node)
{
	return node->parent->arity;
}

hwloc_obj_t node_get_sibling(hwloc_obj_t node, int idx)
{
	return node->parent->children[idx];
}

hwloc_obj_t node_get_child(hwloc_obj_t node, int idx)
{
	return node->children[idx];
}
#endif
*/
import "C"

import (
	"fmt"
	"sync"
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/hardware"
)

type api struct{}

func (a *api) runtimeVersion() uint {
	return uint(C.hwloc_get_api_version())
}

func (a *api) compiledVersion() uint {
	return C.HWLOC_API_VERSION
}

func (a *api) newTopology() (*topology, func(), error) {
	topo := &topology{}
	status := C.hwloc_topology_init(&topo.cTopology)
	if status != 0 {
		return nil, nil, errors.Errorf("hwloc topology init failed: %v", status)
	}

	return topo, func() {
		topo.Lock()
		defer topo.Unlock()
		C.hwloc_topology_destroy(topo.cTopology)
		topo.cTopology = nil
	}, nil
}

// topology is a thin wrapper for the hwloc topology and related functions.
type topology struct {
	sync.RWMutex
	cTopology C.hwloc_topology_t
}

func (t *topology) raw() C.hwloc_topology_t {
	return t.cTopology
}

func (t *topology) getProcessCPUSet(pid int32, flags int) (*cpuSet, func(), error) {
	cpuset := C.hwloc_bitmap_alloc()
	if cpuset == nil {
		return nil, nil, errors.New("hwloc_bitmap_alloc failed")
	}
	cleanup := func() {
		C.hwloc_bitmap_free(cpuset)
	}

	// Access to topology must be synchronized
	t.RLock()
	defer t.RUnlock()
	if status := C.hwloc_get_proc_cpubind(t.cTopology, C.int(pid), cpuset, C.int(flags)); status != 0 {
		cleanup()
		return nil, nil, errors.Errorf("hwloc get proc cpubind failed: %v", status)
	}

	return newCPUSet(t, cpuset), cleanup, nil
}

func (t *topology) setFlags() error {
	t.Lock()
	defer t.Unlock()

	if status := C.topo_setFlags(t.cTopology); status != 0 {
		return errors.Errorf("hwloc set flags failed: %v", status)
	}
	return nil
}

func (t *topology) load() error {
	t.Lock()
	defer t.Unlock()

	if status := C.hwloc_topology_load(t.cTopology); status != 0 {
		return errors.Errorf("hwloc topology load failed: %v", status)
	}
	return nil
}

func (t *topology) getNextObjByType(objType int, prev *object) (*object, error) {
	t.RLock()
	defer t.RUnlock()

	var cPrev C.hwloc_obj_t
	if prev != nil {
		cPrev = prev.cObj
	}
	obj := C.hwloc_get_next_obj_by_type(t.cTopology, C.hwloc_obj_type_t(objType), cPrev)
	if obj == nil {
		return nil, errors.Errorf("no next hwloc object found with type=%d", objType)
	}
	return newObject(t, obj), nil
}

func (t *topology) getNumObjByType(objType int) uint {
	t.RLock()
	defer t.RUnlock()

	return uint(C.hwloc_get_nbobjs_by_type(t.cTopology, C.hwloc_obj_type_t(objType)))
}

const (
	objTypeOSDevice  = C.HWLOC_OBJ_OS_DEVICE
	objTypeBridge    = C.HWLOC_OBJ_BRIDGE
	objTypePCIDevice = C.HWLOC_OBJ_PCI_DEVICE
	objTypeNUMANode  = C.HWLOC_OBJ_NUMANODE
	objTypeCore      = C.HWLOC_OBJ_CORE

	osDevTypeNetwork     = C.HWLOC_OBJ_OSDEV_NETWORK
	osDevTypeOpenFabrics = C.HWLOC_OBJ_OSDEV_OPENFABRICS

	bridgeTypeHost = C.HWLOC_OBJ_BRIDGE_HOST
	bridgeTypePCI  = C.HWLOC_OBJ_BRIDGE_PCI
)

// object is a thin wrapper for hwloc_obj_t and related functions.
type object struct {
	cObj C.hwloc_obj_t
	topo *topology
}

func (o *object) name() string {
	o.topo.RLock()
	defer o.topo.RUnlock()

	objName := C.GoString(o.cObj.name)
	if objName == "" {
		var id string
		switch o.objType() {
		case objTypeBridge:
			lo, hi, err := o.busRange()
			if err != nil {
				id = "(unknown range)"
				break
			}
			id = fmt.Sprintf("%04x:[%02x-%02x]", lo.Domain, lo.Bus, hi.Bus)
		default:
			id = fmt.Sprintf("%d", o.osIndex())
		}
		objName = fmt.Sprintf("%s %s", o.objTypeString(), id)
	}
	return objName
}

func (o *object) osIndex() uint {
	o.topo.RLock()
	defer o.topo.RUnlock()

	return uint(o.cObj.os_index)
}

func (o *object) logicalIndex() uint {
	o.topo.RLock()
	defer o.topo.RUnlock()

	return uint(o.cObj.logical_index)
}

func (o *object) getNumSiblings() uint {
	o.topo.RLock()
	defer o.topo.RUnlock()

	return uint(C.node_get_parent_arity(o.cObj))
}

func (o *object) getSibling(index uint) (*object, error) {
	o.topo.RLock()
	defer o.topo.RUnlock()

	cResult := C.node_get_sibling(o.cObj, C.int(index))
	if cResult == nil {
		return nil, errors.Errorf("sibling of object %q not found", o.name())
	}

	return newObject(o.topo, cResult), nil
}

func (o *object) getNumChildren() uint {
	o.topo.RLock()
	defer o.topo.RUnlock()

	return uint(C.node_get_arity(o.cObj))
}

func (o *object) getChild(index uint) (*object, error) {
	o.topo.RLock()
	defer o.topo.RUnlock()

	if o.cObj.children == nil {
		return nil, errors.Errorf("object %q has no children", o.name())
	}

	if index >= o.getNumChildren() {
		return nil, errors.Errorf("index %d not possible; object %q has %d children", index, o.name(), o.getNumChildren())
	}

	cResult := C.node_get_child(o.cObj, C.int(index))
	return newObject(o.topo, cResult), nil
}

func (o *object) getAncestorByType(objType int) (*object, error) {
	o.topo.RLock()
	defer o.topo.RUnlock()

	cResult := C.hwloc_get_ancestor_obj_by_type(o.topo.raw(), C.hwloc_obj_type_t(objType), o.cObj)
	if cResult == nil {
		return nil, errors.Errorf("type %d ancestor of object %q not found", objType, o.name())
	}

	return newObject(o.topo, cResult), nil
}

func (o *object) getNonIOAncestor() (*object, error) {
	o.topo.RLock()
	defer o.topo.RUnlock()

	cResult := C.hwloc_get_non_io_ancestor_obj(o.topo.raw(), o.cObj)
	if cResult == nil {
		return nil, errors.Errorf("unable to find non-io ancestor for object %q", o.name())
	}

	return newObject(o.topo, cResult), nil
}

func (o *object) cpuSet() *cpuSet {
	o.topo.RLock()
	defer o.topo.RUnlock()

	return newCPUSet(o.topo, o.cObj.cpuset)
}

func (o *object) nodeSet() *nodeSet {
	o.topo.RLock()
	defer o.topo.RUnlock()

	return newNodeSet(o.cObj.nodeset)
}

func (o *object) objType() int {
	o.topo.RLock()
	defer o.topo.RUnlock()

	return int(o.cObj._type)
}

func (o *object) objTypeString() string {
	switch o.objType() {
	case objTypeNUMANode:
		return "NUMA node"
	case objTypeCore:
		return "core"
	case objTypePCIDevice:
		return "PCI device"
	case objTypeBridge:
		return "bridge"
	case objTypeOSDevice:
		return "OS device"
	}
	return fmt.Sprintf("unknown object type %d (0x%x)", o.objType(), o.objType())
}

func (o *object) busRange() (*hardware.PCIAddress, *hardware.PCIAddress, error) {
	o.topo.RLock()
	defer o.topo.RUnlock()

	if o.objType() != objTypeBridge {
		return nil, nil, errors.Errorf("device %q is not a Bridge", o.name())
	}
	downstream := C.node_get_bridge_downstream(o.cObj)

	lo, err := hardware.NewPCIAddress(fmt.Sprintf("%04x:%02x:0.0", downstream.domain, downstream.secondary_bus))
	if err != nil {
		return nil, nil, err
	}
	hi, err := hardware.NewPCIAddress(fmt.Sprintf("%04x:%02x:0.0", downstream.domain, downstream.subordinate_bus))
	if err != nil {
		return nil, nil, err
	}

	return lo, hi, nil
}

func (o *object) bridgeType() (int, error) {
	o.topo.RLock()
	defer o.topo.RUnlock()

	if o.objType() != objTypeBridge {
		return 0, errors.Errorf("device %q is not a Bridge", o.name())
	}
	bridgeType := C.node_get_bridge_type(o.cObj)
	if bridgeType < 0 {
		return 0, errors.Errorf("device %q attrs are nil", o.name())
	}
	return int(bridgeType), nil
}

func (o *object) osDevType() (int, error) {
	o.topo.RLock()
	defer o.topo.RUnlock()

	if o.objType() != objTypeOSDevice {
		return 0, errors.Errorf("device %q is not an OS Device", o.name())
	}
	devType := C.node_get_osdev_type(o.cObj)
	if devType < 0 {
		return 0, errors.Errorf("device %q attrs are nil", o.name())
	}
	return int(devType), nil
}

func (o *object) pciAddr() (*hardware.PCIAddress, error) {
	o.topo.RLock()
	defer o.topo.RUnlock()

	switch o.objType() {
	case objTypePCIDevice, objTypeBridge:
	default:
		return nil, errors.Errorf("device %q is not a PCI Device", o.name())
	}
	pciDevAttr := C.node_get_pcidev_attr(o.cObj)
	if pciDevAttr == nil {
		return nil, errors.Errorf("device %q attrs are nil", o.name())
	}
	return hardware.NewPCIAddress(fmt.Sprintf("%04x:%02x:%02x.%01x", pciDevAttr.domain, pciDevAttr.bus,
		pciDevAttr.dev, pciDevAttr._func))
}

func (o *object) linkSpeed() (float64, error) {
	o.topo.RLock()
	defer o.topo.RUnlock()

	if o.objType() != objTypePCIDevice {
		return 0, errors.Errorf("device %q is not a PCI Device", o.name())
	}
	pciDevAttr := C.node_get_pcidev_attr(o.cObj)
	if pciDevAttr == nil {
		return 0, errors.Errorf("device %q attrs are nil", o.name())
	}
	return float64(pciDevAttr.linkspeed), nil
}

func newObject(topo *topology, cObj C.hwloc_obj_t) *object {
	if cObj == nil {
		panic("nil hwloc_obj_t")
	}
	return &object{
		cObj: cObj,
		topo: topo,
	}
}

type bitmap struct {
	cSet C.hwloc_bitmap_t
}

func (b *bitmap) raw() C.hwloc_bitmap_t {
	return b.cSet
}

func (b *bitmap) String() string {
	var str *C.char

	strLen := C.hwloc_bitmap_asprintf(&str, b.raw())
	if strLen <= 0 {
		return ""
	}
	defer C.free(unsafe.Pointer(str))
	return C.GoString(str)
}

func (b *bitmap) intersectsBitmap(other *bitmap) bool {
	return C.hwloc_bitmap_intersects(b.raw(), other.raw()) != 0
}

func (b *bitmap) isSubsetOfBitmap(other *bitmap) bool {
	return C.hwloc_bitmap_isincluded(b.raw(), other.raw()) != 0
}

type cpuSet struct {
	bitmap
	topo *topology
}

func (c *cpuSet) intersects(other *cpuSet) bool {
	return c.intersectsBitmap(&other.bitmap)
}

func (c *cpuSet) isSubsetOf(other *cpuSet) bool {
	return c.isSubsetOfBitmap(&other.bitmap)
}

func (c *cpuSet) toNodeSet() (*nodeSet, func(), error) {
	nodeset := C.hwloc_bitmap_alloc()
	if nodeset == nil {
		return nil, nil, errors.New("hwloc_bitmap_alloc failed")
	}
	cleanup := func() {
		C.hwloc_bitmap_free(nodeset)
	}

	c.topo.RLock()
	defer c.topo.RUnlock()
	C.hwloc_cpuset_to_nodeset(c.topo.raw(), c.cSet, nodeset)

	return newNodeSet(nodeset), cleanup, nil
}

func mustCPUSetFromString(str string, topo *topology) (*cpuSet, func()) {
	cStr := C.CString(str)
	defer C.free(unsafe.Pointer(cStr))

	result := C.hwloc_bitmap_alloc()
	status := C.hwloc_bitmap_sscanf(result, cStr)
	if status != 0 {
		C.hwloc_bitmap_free(result)
		panic(errors.Errorf("failed to construct bitmap from %q, status=%d", str, status))
	}

	return newCPUSet(topo, result), func() { C.hwloc_bitmap_free(result) }
}

type nodeSet struct {
	bitmap
}

func (n *nodeSet) intersects(other *nodeSet) bool {
	return n.intersectsBitmap(&other.bitmap)
}

func (c *nodeSet) isSubsetOf(other *nodeSet) bool {
	return c.isSubsetOfBitmap(&other.bitmap)
}

func newCPUSet(topo *topology, cSet C.hwloc_bitmap_t) *cpuSet {
	return &cpuSet{
		bitmap: bitmap{
			cSet: cSet,
		},
		topo: topo,
	}
}

func newNodeSet(cSet C.hwloc_bitmap_t) *nodeSet {
	return &nodeSet{
		bitmap: bitmap{
			cSet: cSet,
		},
	}
}
