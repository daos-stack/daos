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

import (
	"fmt"

	"github.com/daos-stack/daos/src/control/logging"
)

const (
	direct = iota
	sibling
	bestfit
	libFabricMajorVersion = 1
	libFabricMinorVersion = 7
	hwlocFlagsStandard    = 0
	hwlocFlagsWholeSystem = 1
	allHFIUsed            = -1
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
	Provider   string
	DeviceName string
	NUMANode   uint
	Priority   int
}

func (fs FabricScan) String() string {
	return fmt.Sprintf("\tfabric_iface: %v\n\tprovider: %v\n\tpinned_numa_node: %d", fs.DeviceName, fs.Provider, fs.NUMANode)
}

// DeviceScan caches initialization data for later hwloc usage
type DeviceScan struct {
	topology             string
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

// GetAffinityForNetworkDevices searches the system topology reported by hwloc
// for the devices specified by deviceNames and returns the corresponding
// name, cpuset, nodeset and NUMA node information for each device it finds.
//
// The input deviceNames []string specifies names of each network device to
// search for. Typical network device names are "eth0", "eth1", "ib0", etc.
//
// The DeviceAffinity DeviceName matches the deviceNames strings and should be
// used to help match an input device with the output.
//
// Network device names that are not found in the topology are ignored.
// The order of network devices in the return string depends on the natural
// order in the system topology and does not depend on the order specified
// by the input string.
func GetAffinityForNetworkDevices(deviceNames []string) ([]DeviceAffinity, error) {
	var affinity []DeviceAffinity
	// return affinity, errors.New("functionality stubbed")
	return affinity, nil
}

// GetDeviceAlias is a complete method to find an alias for the device name provided.
// For example, the device alias for "ib0" is a sibling node in the hwloc topology
// with the name "hfi1_0".
func GetDeviceAlias(device string) (string, error) {
	return "", nil
}

// GetAffinityForDevice searches the system topology reported by hwloc
// for the device specified by netDeviceName and returns the corresponding
// name, cpuset, nodeset, and NUMA node ID information.
//
// Call initDeviceScan() to initialize the hwloc library and then set the
// deviceScanCfg.targetDevice to a network device name prior to calling this function.
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
	return DeviceAffinity{}, nil
}

// GetDeviceNames examines the network interfaces
// and returns a []string identifying them by name
func GetDeviceNames() ([]string, error) {
	return []string{}, nil
}

// GetSupportedProviders returns a []string containing all supported Mercury providers
func GetSupportedProviders() []string {
	return []string{}
}

// ValidateProviderStub is used for most unit testing to replace ValidateProviderConfig because the network configuration
// validation depends upon physical hardware resources and configuration on the target machine
// that are either not known or static in the test environment
func ValidateProviderStub(device string, provider string) error {
	return nil
}

// ValidateProviderConfig confirms that the given network device supports the chosen provider
func ValidateProviderConfig(device string, provider string) error {
	return nil
}

// ValidateNUMAStub is used for most unit testing to replace ValidateNUMAConfig because the network configuration
// validation depends upon physical hardware resources and configuration on the target machine
// that are either not known or static in the test environment
func ValidateNUMAStub(device string, numaNode uint) error {
	return nil
}

// ValidateNUMAConfig confirms that the given network device matches the NUMA ID given.
func ValidateNUMAConfig(device string, numaNode uint) error {
	return nil
}

// ScanFabric examines libfabric data to find the network devices that support the given fabric provider.
func ScanFabric(provider string) ([]FabricScan, error) {
	var ScanResults []FabricScan
	return ScanResults, nil
}
