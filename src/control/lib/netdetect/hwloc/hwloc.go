//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// hwloc is a set of Go bindings for interacting with the hwloc library.
package hwloc

import (
	"github.com/pkg/errors"
)

// API is an interface for basic API operations, including synchronizing access.
type API interface {
	Lock()
	Unlock()

	runtimeVersion() uint
	compiledVersion() uint
	newTopology() (Topology, func(), error)
}

// Topology is an interface for a hardware topology.
type Topology interface {
	// GetProcCPUBind gets a CPUSet associated with a given pid, and returns the CPUSet and its
	// cleanup function, or an error.
	GetProcCPUBind(pid int32, flags int) (CPUSet, func(), error)
	// SetFlags sets the desired flags on the topology.
	SetFlags() error
	// Load loads the topology.
	Load() error
	// GetObjByDepth gets an Object at a given depth and index in the topology.
	GetObjByDepth(depth int, index uint) (Object, error)
	// GetTypeDepth fetches the depth of a given ObjType in the topology.
	GetTypeDepth(objType ObjType) int
	// GetNumObjAtDepth fetches the number of objects located at a given depth in the topology.
	GetNumObjAtDepth(depth int) uint
}

// Object is an interface for an object in a Topology.
type Object interface {
	// Name returns the name of the object.
	Name() string
	// LogicalIndex returns the logical index of the object.
	LogicalIndex() uint
	// GetNumSiblings returns the number of siblings this object has in the topology.
	GetNumSiblings() uint
	// GetChild gets the object's child with a given index, or returns an error.
	GetChild(index uint) (Object, error)
	// GetAncestorByType gets the object's ancestor of a given type, or returns an error.
	GetAncestorByType(objType ObjType) (Object, error)
	// GetNonIOAncestor gets the object's non-IO ancestor, if any, or returns an error.
	GetNonIOAncestor() (Object, error)
	// CPUSet gets the CPUSet associated with the object.
	CPUSet() CPUSet
	// NodeSet gets the NodeSet associated with the object.
	NodeSet() NodeSet
}

// CPUSet is an interface for a CPU set.
type CPUSet interface {
	String() string
	// Intersects determines if this CPUSet intersects with another one.
	Intersects(CPUSet) bool
	// IsSubsetOf determines if this CPUSet is included completely within another one.
	IsSubsetOf(CPUSet) bool
	// ToNodeSet translates this CPUSet into a NodeSet and returns it with its cleanup function,
	// or returns an error.
	ToNodeSet() (NodeSet, func(), error)
}

// NodeSet is an interface for a node set.
type NodeSet interface {
	String() string
	// Intersects determines if this NodeSet intersects with another one.
	Intersects(NodeSet) bool
}

// GetAPI fetches an API reference.
func GetAPI() API {
	return &api{}
}

// GetTopology initializes the hwloc topology and returns it to the caller along with the topology
// cleanup function.
func GetTopology(api API) (Topology, func(), error) {
	if api == nil {
		return nil, nil, errors.New("nil API")
	}

	if err := checkVersion(api); err != nil {
		return nil, nil, err
	}

	var topo Topology
	var cleanup func()
	var err error

	api.Lock()
	defer api.Unlock()

	topo, cleanup, err = api.newTopology()
	if err != nil {
		return nil, nil, err
	}
	defer func() {
		if err != nil {
			cleanup()
		}
	}()

	if err = topo.SetFlags(); err != nil {
		return nil, nil, err
	}

	if err = topo.Load(); err != nil {
		return nil, nil, err
	}

	return topo, cleanup, nil
}

// checkVersion verifies the runtime API is compatible with the compiled version of the API.
func checkVersion(api API) error {
	version := api.runtimeVersion()
	compVersion := api.compiledVersion()
	if (version >> 16) != (compVersion >> 16) {
		return errors.Errorf("hwloc API incompatible with runtime: compiled for version 0x%x but using 0x%x\n",
			compVersion, version)
	}
	return nil
}
