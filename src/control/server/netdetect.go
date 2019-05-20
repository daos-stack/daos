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

import (
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/log"
	"github.com/pkg/errors"
	"os/exec"
	"net"
	"strings"
)

const applicationPath = "/bin/lstopo"

// detectTopology uses lstopo application to retrieve the network device
// topology for all NUMA Nodes found on this system.
// Returns a string containing a hierarchical list of NUMA Nodes and any
// network devices connected to them.  If the lstopo application cannot be
// found at the given location, detectTopology() will search for it and
// try again.

// TODO -- pass in external interface for exec so this can be mocked
func detectTopology(path string) (string, error) {
	cmd, err := common.GetAbsInstallPath(path)
	args := []string{"--ignore", "core", "--ignore", "cache", "--ignore", "pu"}
	out, err := exec.Command(cmd, args...).Output()

	// If execution fails from the given path, 
	// look for the lstopo application and try again
	if err != nil {
		out, err = exec.Command("which","lstopo").Output()
		if err != nil {
			log.Debugf("Unable to find lstopo.  Cannot detect topology." +
				"Error: %v", err)
			return "", err
		}

		path := strings.SplitAfter(string(out), "lstopo")
		out, err = exec.Command(path[0], args...).Output()
		if err != nil {
				log.Debugf("Uable to detect device topology.  Error: %v", err)
				return "", err
		}
	}

	return string(out), nil
}

// DetectNUMANodeForDevices will lookup each network device specified
// by the input string in the system topology obtained by detectTopology()
// The input string has the form "device0,device1,...deviceN"
// where 'device' specifies a network device such as "eth0", "eth1", or "ib0"
// The return string has the form: 
// "dev0:logical:physical,dev1:logical:physical,...devN:logical:physical"
// where dev is the name of the network device, logical is the NUMA Node logical ID
// and physical is the NUMA Node physical ID
// network device names that are not found in the topology are ignored.
func DetectNUMANodeForDevices(netNames string) (string, error) {
	var numaDeviceStr string

	hwlocStr, err := detectTopology(applicationPath)
	if err != nil {
		return "", err
	}

	if !strings.Contains(hwlocStr, "NUMANode") {
		return numaDeviceStr, errors.New("the NUMA Node information not found")
	}

	numNUMANodes := strings.Count(hwlocStr, "NUMANode")

	// Separate the string into fields representing each NUMA Node
	fields := strings.Split(hwlocStr, "NUMANode")
	// Skip past the header information
	fields = fields[1:]

	devices := strings.Split(netNames, ",")
	deviceFound := make([]bool, len(devices))
	
	for i := 0; i < numNUMANodes; i++ {
		if !strings.Contains(fields[i], "L#") {
			return "", errors.New("invalid topology data: L# not found")
		}
		logicalNodeStr := strings.SplitAfter(fields[i], "L#")
		logicalNodeStr = strings.Split(logicalNodeStr[1], " ")

		if !strings.Contains(fields[i], "P#") {
			return "", errors.New("invalid topology:  P# not found")
		}
		physicalNodeStr := strings.SplitAfter(fields[i], "P#")
		physicalNodeStr = strings.Split(physicalNodeStr[1], " ")

		for j := 0; j < len(devices); j++ {
			if len(devices[j]) > 0  && !deviceFound[j] {
				srchStr := "\"" + devices[j] + "\""
				if strings.Contains(fields[i], srchStr) {
					numaDeviceStr += devices[j] + ":" + logicalNodeStr[0] + ":" +
						physicalNodeStr[0] + ","
					// Mark the device as found so we don't look for it
					// again in the remaining NUMANodes
					deviceFound[j] = true
				}
			}
		}
	}

	return strings.TrimSuffix(numaDeviceStr, ","), nil
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