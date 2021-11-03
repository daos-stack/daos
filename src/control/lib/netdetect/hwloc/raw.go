//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package hwloc

/*
#cgo LDFLAGS: -lhwloc
#include <hwloc.h>

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
	"sync"
	"unsafe"

	"github.com/pkg/errors"
)

type api struct {
	sync.Mutex
}

func (a *api) runtimeVersion() uint {
	return uint(C.hwloc_get_api_version())
}

func (a *api) compiledVersion() uint {
	return C.HWLOC_API_VERSION
}

func (a *api) newTopology() (Topology, func(), error) {
	topo := &topology{
		api: a,
	}
	status := C.hwloc_topology_init(&topo.cTopology)
	if status != 0 {
		return nil, nil, errors.Errorf("hwloc topology init failed: %v", status)
	}

	return topo, func() {
		C.hwloc_topology_destroy(topo.cTopology)
	}, nil
}

// topology is a thin wrapper for the hwloc topology and related functions.
type topology struct {
	api       API
	cTopology C.hwloc_topology_t
}

func (t *topology) raw() C.hwloc_topology_t {
	return t.cTopology
}

func (t *topology) GetProcCPUBind(pid int32, flags int) (CPUSet, func(), error) {
	// Access to hwloc_get_proc_cpubind must be synchronized
	t.api.Lock()
	defer t.api.Unlock()

	cpuset := C.hwloc_bitmap_alloc()
	if cpuset == nil {
		return nil, nil, errors.New("hwloc_bitmap_alloc failed")
	}
	cleanup := func() {
		C.hwloc_bitmap_free(cpuset)
	}

	if status := C.hwloc_get_proc_cpubind(t.cTopology, C.int(pid), cpuset, C.int(flags)); status != 0 {
		cleanup()
		return nil, nil, errors.Errorf("hwloc get proc cpubind failed: %v", status)
	}

	return newCPUSet(t, cpuset), cleanup, nil
}

func (t *topology) SetFlags() error {
	if status := C.cmpt_setFlags(t.cTopology); status != 0 {
		return errors.Errorf("hwloc set flags failed: %v", status)
	}
	return nil
}

func (t *topology) Load() error {
	if status := C.hwloc_topology_load(t.cTopology); status != 0 {
		return errors.Errorf("hwloc topology load failed: %v", status)
	}
	return nil
}

func (t *topology) GetObjByDepth(depth int, index uint) (Object, error) {
	obj := C.cmpt_get_obj_by_depth(t.cTopology, C.int(depth), C.uint(index))
	if obj == nil {
		return nil, errors.Errorf("no hwloc object found with depth=%d, index=%d", depth, index)
	}
	return newObject(t, obj), nil
}

func (t *topology) GetTypeDepth(objType ObjType) int {
	return int(C.hwloc_get_type_depth(t.cTopology, C.hwloc_obj_type_t(objType)))
}

func (t *topology) GetNumObjAtDepth(depth int) uint {
	return uint(C.cmpt_get_nbobjs_by_depth(t.cTopology, C.int(depth)))
}

type ObjType C.hwloc_obj_type_t

const (
	ObjTypeOSDevice = ObjType(C.HWLOC_OBJ_OS_DEVICE)
	ObjTypeNUMANode = ObjType(C.HWLOC_OBJ_NUMANODE)
	ObjTypeCore     = ObjType(C.HWLOC_OBJ_CORE)

	TypeDepthOSDevice = C.HWLOC_TYPE_DEPTH_OS_DEVICE
	TypeDepthUnknown  = C.HWLOC_TYPE_DEPTH_UNKNOWN
)

type rawTopology interface {
	raw() C.hwloc_topology_t
}

// object is a thin wrapper for hwloc_obj_t and related functions.
type object struct {
	cObj C.hwloc_obj_t
	topo rawTopology
}

func (o *object) Name() string {
	return C.GoString(o.cObj.name)
}

func (o *object) LogicalIndex() uint {
	return uint(o.cObj.logical_index)
}

func (o *object) GetNumSiblings() uint {
	return uint(C.cmpt_get_parent_arity(o.cObj))
}

func (o *object) GetChild(index uint) (Object, error) {
	cResult := C.cmpt_get_child(o.cObj, C.int(index))
	if cResult == nil {
		return nil, errors.Errorf("child of object %q not found", o.Name())
	}

	return newObject(o.topo, cResult), nil
}

func (o *object) GetAncestorByType(objType ObjType) (Object, error) {
	cResult := C.hwloc_get_ancestor_obj_by_type(o.topo.raw(), C.hwloc_obj_type_t(objType), o.cObj)
	if cResult == nil {
		return nil, errors.Errorf("type %v ancestor of object %q not found", objType, o.Name())
	}

	return newObject(o.topo, cResult), nil
}

func (o *object) GetNonIOAncestor() (Object, error) {
	ancestorNode := C.hwloc_get_non_io_ancestor_obj(o.topo.raw(), o.cObj)
	if ancestorNode == nil {
		return nil, errors.New("unable to find non-io ancestor node for device")
	}

	return newObject(o.topo, ancestorNode), nil
}

func (o *object) CPUSet() CPUSet {
	return newCPUSet(o.topo, o.cObj.cpuset)
}

func (o *object) NodeSet() NodeSet {
	return newNodeSet(o.cObj.nodeset)
}

func newObject(topo rawTopology, cObj C.hwloc_obj_t) Object {
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

func (b *bitmap) intersects(other *bitmap) bool {
	return C.hwloc_bitmap_intersects(b.raw(), other.raw()) != 0
}

func (b *bitmap) isSubsetOf(other *bitmap) bool {
	return C.hwloc_bitmap_isincluded(b.raw(), other.raw()) != 0
}

type cpuSet struct {
	bitmap
	topo rawTopology
}

func (c *cpuSet) Intersects(other CPUSet) bool {
	c2, ok := other.(*cpuSet)
	if !ok {
		return false
	}
	return c.intersects(&c2.bitmap)
}

func (c *cpuSet) IsSubsetOf(other CPUSet) bool {
	c2, ok := other.(*cpuSet)
	if !ok {
		return false
	}
	return c.isSubsetOf(&c2.bitmap)
}

func (c *cpuSet) ToNodeSet() (NodeSet, func(), error) {
	nodeset := C.hwloc_bitmap_alloc()
	if nodeset == nil {
		return nil, nil, errors.New("hwloc_bitmap_alloc failed")
	}
	cleanup := func() {
		C.hwloc_bitmap_free(nodeset)
	}
	C.hwloc_cpuset_to_nodeset(c.topo.raw(), c.cSet, nodeset)

	return newNodeSet(nodeset), cleanup, nil
}

type nodeSet struct {
	bitmap
}

func (n *nodeSet) Intersects(other NodeSet) bool {
	n2, ok := other.(*nodeSet)
	if !ok {
		return false
	}
	return n.intersects(&n2.bitmap)
}

func newCPUSet(topo rawTopology, cSet C.hwloc_bitmap_t) CPUSet {
	return &cpuSet{
		bitmap: bitmap{
			cSet: cSet,
		},
		topo: topo,
	}
}

func newNodeSet(cSet C.hwloc_bitmap_t) NodeSet {
	return &nodeSet{
		bitmap: bitmap{
			cSet: cSet,
		},
	}
}
