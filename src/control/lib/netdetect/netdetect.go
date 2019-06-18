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
#cgo LDFLAGS: -lhwloc
#include <stdlib.h>
#include <hwloc.h>
#include <stdio.h>
*/
import "C"
import "unsafe"

import (
	"fmt"
	"net"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/log"
)

type DeviceAffinity struct {
	DeviceName	string
	CPUSet	string
	NodeSet	string
}

func (da *DeviceAffinity) String() string {
	return fmt.Sprintf("%s:%s:%s", da.DeviceName, da.CPUSet, da.NodeSet)
}

// initLib initializes the hwloc library.
func initLib() (C.hwloc_topology_t, error) {
	var topology C.hwloc_topology_t

	status := C.hwloc_topology_init(&topology)
	if status != 0 {
		return nil, errors.New("hwloc_topology_init failure")
	}

	status = C.hwloc_topology_set_flags(topology, C.HWLOC_TOPOLOGY_FLAG_IO_DEVICES)
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

// GetAffinityForNetworkDevices searches the system topology reported by hwloc
// for the devices specified by deviceNames and returns the corresponding
// name, cpuset and nodeset information for each device it finds.
//
// The input deviceNames []string specifies names of each network device to
// search for. Typical network device names are "eth0", "eth1", "ib0", etc.
//
// The DeviceAffinity struct specifies a name, cpuset and nodeset strings.
// The DeviceAffinity DeviceName matches the deviceNames strings and should be
// used to help match an input device with the output.
// The DeviceAffinity CPUSet and NodeSet are string representations of the
// corresponding hwloc bitmaps.  When converted back to hwloc_bitmap_t via
// hwloc_bitmap_sscanf() these bitmaps are used by the hwloc API to bind a
// thread to processing units that are closest to the given network device.
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

	topology, err := initLib()
	if (err != nil) {
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
		if cpusetLen > 0 && nodesetLen > 0 {
			deviceNode := DeviceAffinity{
				DeviceName : C.GoString(node.name),
				CPUSet : C.GoString(cpuset),
				NodeSet : C.GoString(nodeset),
			}
			affinity = append(affinity, deviceNode)
		}
		C.free(unsafe.Pointer(cpuset))
		C.free(unsafe.Pointer(nodeset))
	}
	cleanUp(topology)
	return affinity, nil
}

// GetDeviceNames examines the network interfaces
// and returns a []string identifying them by name
func GetDeviceNames() ([]string, error) {
	networkInterfaces, err := net.Interfaces()

	if err != nil {
		log.Debugf("Error while detecting network interfaces: %s", err)
		return nil, err
	}

	netNames := make([]string, 0, len(networkInterfaces))
	for _, i := range networkInterfaces {
		netNames = append(netNames, i.Name)
	}
	return netNames, nil
}
