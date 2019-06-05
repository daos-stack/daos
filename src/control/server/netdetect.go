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

// GetAffinityForNetworkDevices returns the ioDevices, cpuset and nodeset
// that intersect with the devices specified by the netNames.  The netNames
// string must be in the format specified by DetectNetworkDevices() while
// the ioDevices string must be in the format specified by
// GetAffinityForIONodes().
func GetAffinityForNetworkDevices(netNames string, ioDevices string) (string) {
	var networkDeviceSubset string

	netNameList := strings.Split(netNames, ",")
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

// DetectNetworkDevices examines the network interfaces
// and returns a string identifying them by name and has the form:
// "device0,device1, ... deviceN"
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

// DetectAffinityForNetworkDevices searches the local system topology for each
// network device and returns a string that names each device and the
// corresponding cpuset and nodeset.  The cpuset describes those cpus with
// the highest spatial locality to the device and are used by the hwloc API
// to bind a thread to processing units that are closely connected.
// Use hwloc_bitmap_sscanf() to convert the cpuset and nodeset strings back
// into hwloc_bitmap_t when ready to bind with the hwloc API.
func DetectAffinityForNetworkDevices() (string, error) {
	var affinity string
	var ioDevices *C.char

	netDevsList, err := DetectNetworkDevices()
	if err != nil {
		return affinity, err
	}
	log.Debugf("NetDevsList %s", netDevsList)

	// Dynamically load the hwloc library, map the exported functions
	// and initialize the library to get it ready for use.
	status := C.InitializeTopologyLib(C.CString(hwlocLibrary))
	if (status != C.SUCCESS) {
		return affinity, errors.New("Unable to load and initialize hwloc " +
			"library.  Non fatal error.")
	}

	// Walk the topology to retrieve a list of IO nodes and their cpusets
	// and nodesets
	ioDevices = C.GetAffinityForIONodes()

	// Store the results as a GO string and free the underlying C string
	// allocated by GetAffinityForIONodes()
	tempStr := C.GoString(ioDevices)
	C.free(unsafe.Pointer(ioDevices))

	// Extract the data that pertains to the network devices found
	// because the ioDevices string has a superset of the data we want
	affinity = GetAffinityForNetworkDevices(netDevsList, tempStr)

	return affinity, nil
}