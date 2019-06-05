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
#cgo LDFLAGS: -ldl
#include <stdlib.h>
#include "netdetect.h"
*/
import "C"
import "unsafe"

import (
	"github.com/daos-stack/daos/src/control/log"
	"github.com/pkg/errors"
	"net"
	"strings"
)

const hwlocLibrary = "libhwloc.so"

// extractAffinityForNamedDevices returns the ioDevices, cpuset and nodeset
// that intersect with the devices specified by the devices.  The devices
// string must be in the format "name0,name1, ... nameN" while
// the ioDevices string must be in the format specified by:
// "device0:cpuset:nodeset;device1:cpuset:nodeset; ... deviceN:cpuset:nodeset"
func extractAffinityForNamedDevices(devices string, ioDevices string) (string) {
	var networkDeviceSubset string

	if devices == "" || ioDevices == "" {
		return networkDeviceSubset
	}

	netNameList := strings.Split(devices, ",")
	allDevices := strings.Split(ioDevices, ";")
	lenNetNameList := len(netNameList)
	lenAllDevices := len(allDevices)

	for i := 0; i < lenNetNameList; i++ {
		for j := 0; j < lenAllDevices; j++ {
			if strings.Contains(allDevices[j], netNameList[i]) {
				networkDeviceSubset += allDevices[j] + ";"
				break;
			}
		}
	}
	return strings.TrimSuffix(networkDeviceSubset, ";")
}

// detectNetworkDevices examines the local network interfaces and
// returns a string identifying them by name with the form:
// "device0,device1, ... deviceN"
func detectNetworkDevices() (string, error) {
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

// getAffinityForNetworkDevices searches the local system topology for each
// network device and returns a string that names each device and the
// corresponding cpuset and nodeset.  The cpuset describes those cpus with
// the highest spatial locality to the device and are used by the hwloc API
// to bind a thread to processing units that are closely connected.
// Use hwloc_bitmap_sscanf() to convert the cpuset and nodeset strings back
// into hwloc_bitmap_t when ready to bind with the hwloc API.
func getAffinityForNetworkDevices() (string, error) {
	var affinity string
	var ioDevices *C.char

	// Dynamically load the hwloc library, map the exported functions
	// and initialize the library to get it ready for use.
	status := C.NetDetectInitialize(C.CString(hwlocLibrary))
	if (status != C.NETDETECT_SUCCESS) {
		return affinity, errors.New("Unable to load and initialize hwloc " +
			"library.  Non fatal error.")
	}

	// Walk the topology to retrieve a list of IO devices and their cpusets
	// and nodesets
	ioDevices = C.NetDetectGetAffinityForIONodes()

	// Store the results as a GO string and free the underlying C string
	// allocated by NetDetectGetAffinityForIONodes()
	temp := C.GoString(ioDevices)
	C.free(unsafe.Pointer(ioDevices))

	// We're done with the libary, so clean up.
	status = C.NetDetectCleanup()

	// Query the system to determine the names of the network devices
	netDevsList, err := detectNetworkDevices()
	if err != nil {
		return affinity, err
	}
	// Extract the data that pertains to the network devices found
	affinity = extractAffinityForNamedDevices(netDevsList, temp)

	return affinity, nil
}