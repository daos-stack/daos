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

package main
/*
#cgo CFLAGS: -I${SRCDIR}/../../include
#cgo LDFLAGS: -lhwloc
#include <stdlib.h>
#include <hwloc.h>
#include <stdio.h>
*/
import "C"
import "unsafe"

import (
	"github.com/daos-stack/daos/src/control/log"
	"github.com/pkg/errors"
	"net"
	"strings"
)

// GetAffinityForNetworkDevice searches the system topology for each network
// device specified by the input string and returns corresponding cpuset and
// nodeset bitmaps as strings.  When converted back to hwloc_bitmap_t via
// hwloc_bitmap_sscanf() these bitmaps are used by the hwloc API to bind a
// thread to processing units that are closest to the given network device.
//
// The input string has the form "device0,device1,...deviceN"
// where 'device' specifies a network device such as "eth0", "eth1", or "ib0"
// The return string has the form:
// "dev0:cpuset:nodeset;dev1:cpuset:nodeset; ... devN:cpuset:nodeset"
// where dev is the name of the network device and cpuset and nodeset are
// the corresponding bitmap strings.
//
// Network device names that are not found in the topology are ignored.
// The order of network devices in the return string depends on the natural
// order in the system topology and does not depend on the order specified
// by the input string.
func GetAffinityForNetworkDevice(netNames string) (string, error) {
	var affinity string
	var topology C.hwloc_topology_t
	var node C.hwloc_obj_t
	var i C.uint
	var cpuset *C.char
	var nodeset *C.char

	err := C.hwloc_topology_init(&topology)
	if err != C.int(0) {
		return affinity, errors.New("hwloc_topology_init failure")
	}

	err = C.hwloc_topology_set_flags(topology,
		C.HWLOC_TOPOLOGY_FLAG_IO_DEVICES)
	if err != C.int(0) {
		return affinity, errors.New("hwloc_topology_set_flags failure")
	}

	err = C.hwloc_topology_load(topology)
	if err != C.int(0) {
		return affinity, errors.New("hwloc_topology_load failure")
	}

	depth := C.hwloc_get_type_depth(topology, C.HWLOC_OBJ_OS_DEVICE)
	if depth != C.HWLOC_TYPE_DEPTH_OS_DEVICE {
		return affinity,
			errors.New("hwloc_get_type_depth returned invalid value")
	}

	numObj := C.hwloc_get_nbobjs_by_depth(topology, C.uint(depth))
	
	// for any OS object found in the network device list,
	// store the cpuset and nodeset of the parent NUMA node or oldest
	// non-root ancestor containing this object
	for i = 0; i < numObj; i++ {
		node = C.hwloc_get_obj_by_depth(topology, C.uint(depth), i)
		if node != nil && strings.Contains(netNames, C.GoString(node.name)) {
			ancestorNode := C.hwloc_get_ancestor_obj_by_type(topology,
				C.HWLOC_OBJ_NUMANODE, node)
			// If there is no NUMANODE ancestor, then look for the oldest
			// non-root level ancestor (i.e. at depth 1)
			if ancestorNode == nil {
				ancestorNode = C.hwloc_get_ancestor_obj_by_depth(topology,
					1, node)
			}
			if ancestorNode != nil {
				cpusetLen := C.hwloc_bitmap_asprintf(&cpuset,
					ancestorNode.cpuset)
				nodesetLen := C.hwloc_bitmap_asprintf(&nodeset,
					ancestorNode.nodeset)
				if cpusetLen > 0 && nodesetLen > 0 {
					affinity += C.GoString(node.name) + ":" +
						C.GoString(cpuset) + ":" + C.GoString(nodeset) + ";"
				}
				C.free(unsafe.Pointer(cpuset))
				C.free(unsafe.Pointer(nodeset))
			}
		}
	}
	C.hwloc_topology_destroy(topology)
	return strings.TrimSuffix(affinity, ";"), nil
}

// DetectNetworkDevices examines the network interfaces
// and returns a string identifying them by name and has the form:
// "device0,device1,...deviceN"
func DetectNetworkDevices() (string, error) {
	var netNames string
	networkInterfaces, err := net.Interfaces()

	if err != nil {
		log.Debugf("Error while detecting network interfaces: %s", err)
		return netNames, err
	}

	for _, i := range networkInterfaces {
		netNames += i.Name + ","
	}

	return strings.TrimSuffix(netNames, ","), nil
}
