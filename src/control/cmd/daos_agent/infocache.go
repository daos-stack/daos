//
// (C) Copyright 2020 Intel Corporation.
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
	"context"
	"strings"
	"sync"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	invalidIndex         = -1
	verbsProvider        = "ofi+verbs"
	defaultNetworkDevice = "lo"
	defaultDomain        = "lo"
)

type attachInfoCache struct {
	log logging.Logger
	// is caching enabled?
	enabled atm.Bool
	// is the cache initialized?
	initialized atm.Bool
	// maps NUMA affinity and device index to a response
	numaDeviceMarshResp map[int]map[int][]byte
	// maps NUMA affinity to a device index
	currentNumaDevIdx map[int]int
	mutex             sync.Mutex
	// specifies what NUMA node to use when there are no devices
	// associated with the client NUMA node
	defaultNumaNode int
}

// loadBalance is a simple round-robin load balancing scheme
// to assign network interface adapters to clients
// on the same NUMA node that have multiple adapters
// to choose from.  Returns the index of the device to use.
func (aic *attachInfoCache) loadBalance(numaNode int) int {
	aic.mutex.Lock()
	deviceIndex := invalidIndex
	numDevs := len(aic.numaDeviceMarshResp[numaNode])
	if numDevs > 0 {
		deviceIndex = aic.currentNumaDevIdx[numaNode]
		aic.currentNumaDevIdx[numaNode] = (deviceIndex + 1) % numDevs
	}
	aic.mutex.Unlock()
	return deviceIndex
}

func (aic *attachInfoCache) getResponse(numaNode int) ([]byte, error) {
	deviceIndex := aic.loadBalance(numaNode)
	// If there is no response available for the client's actual NUMA node,
	// use the default NUMA node
	if deviceIndex == invalidIndex {
		deviceIndex = aic.loadBalance(aic.defaultNumaNode)
		if deviceIndex == invalidIndex {
			return nil, errors.Errorf("No default response found for the default NUMA node %d", aic.defaultNumaNode)
		}
		aic.log.Infof("No network devices bound to client NUMA node %d.  Using response from NUMA %d", numaNode, aic.defaultNumaNode)
		numaNode = aic.defaultNumaNode
	}

	aic.mutex.Lock()
	defer aic.mutex.Unlock()
	numaDeviceMarshResp, ok := aic.numaDeviceMarshResp[numaNode][deviceIndex]
	if !ok {
		return nil, errors.Errorf("GetAttachInfo entry for numaNode %d device index %d did not exist", numaNode, deviceIndex)
	}

	aic.log.Debugf("Retrieved response for NUMA %d with device index %d\n", numaNode, deviceIndex)
	return numaDeviceMarshResp, nil
}

func (aic *attachInfoCache) isCached() bool {
	return aic.enabled.IsTrue() && aic.initialized.IsTrue()
}

// initResponseCache generates a unique dRPC response corresponding to each device specified
// in the scanResults.  The responses are differentiated based on the network device NUMA affinity.
func (aic *attachInfoCache) initResponseCache(ctx context.Context, resp *mgmtpb.GetAttachInfoResp, scanResults []*netdetect.FabricScan) error {
	aic.mutex.Lock()
	defer aic.mutex.Unlock()

	// Make a new map each time the cache is initialized
	aic.numaDeviceMarshResp = make(map[int]map[int][]byte)

	// Make a new map just once.
	// Preserve any previous device index map in order to maintain ability to load balance
	if len(aic.currentNumaDevIdx) == 0 {
		aic.currentNumaDevIdx = make(map[int]int)
	}

	var haveDefaultNuma bool

	for _, fs := range scanResults {
		if fs.DeviceName == "lo" {
			continue
		}

		if fs.NetDevClass != resp.NetDevClass {
			aic.log.Debugf("Excluding device: %s, network device class: %s from attachInfoCache.  Does not match server network device class: %s\n",
				fs.DeviceName, netdetect.DevClassName(fs.NetDevClass), netdetect.DevClassName(resp.NetDevClass))
			continue
		}

		resp.Interface = fs.DeviceName
		// by default, the domain is the deviceName
		resp.Domain = fs.DeviceName
		if strings.HasPrefix(resp.Provider, verbsProvider) {
			deviceAlias, err := netdetect.GetDeviceAlias(ctx, resp.Interface)
			if err != nil {
				aic.log.Debugf("non-fatal error: %v. unable to determine OFI_DOMAIN for %s", err, resp.Interface)
			} else {
				resp.Domain = deviceAlias
				aic.log.Debugf("OFI_DOMAIN has been detected as: %s", resp.Domain)
			}
		}

		numa := int(fs.NUMANode)

		numaDeviceMarshResp, err := proto.Marshal(resp)
		if err != nil {
			return drpc.MarshalingFailure()
		}

		if _, ok := aic.numaDeviceMarshResp[numa]; !ok {
			aic.numaDeviceMarshResp[numa] = make(map[int][]byte)
		}
		aic.numaDeviceMarshResp[numa][len(aic.numaDeviceMarshResp[numa])] = numaDeviceMarshResp

		// Any client bound to a NUMA node that has no network devices associated with it will
		// get a response from this defaultNumaNode.
		// This response offers a valid network device at degraded performance for those clients
		// that need it.
		if !haveDefaultNuma {
			aic.defaultNumaNode = numa
			haveDefaultNuma = true
			aic.log.Debugf("The default NUMA node is: %d", aic.defaultNumaNode)
		}

		aic.log.Debugf("Added device %s, domain %s for NUMA %d, device number %d\n", resp.Interface, resp.Domain, numa, len(aic.numaDeviceMarshResp[numa])-1)
	}

	// If there were no network devices found, then add a default response to the default NUMA node entry
	if _, ok := aic.numaDeviceMarshResp[aic.defaultNumaNode]; !ok {
		aic.log.Info("No network devices detected in fabric scan; default AttachInfo response may be incorrect\n")
		aic.numaDeviceMarshResp[aic.defaultNumaNode] = make(map[int][]byte)
		resp.Interface = defaultNetworkDevice
		resp.Domain = defaultDomain
		numaDeviceMarshResp, err := proto.Marshal(resp)
		if err != nil {
			return drpc.MarshalingFailure()
		}
		aic.numaDeviceMarshResp[aic.defaultNumaNode][0] = numaDeviceMarshResp
	}

	// If caching is enabled, the cache is now 'initialized'
	if aic.enabled.IsTrue() {
		aic.initialized.SetTrue()
	}

	return nil
}
