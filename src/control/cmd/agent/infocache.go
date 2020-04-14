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

const defaultNumaNode = 0

type attachInfoCache struct {
	log          logging.Logger
	// is caching enabled?
	enabled      atm.Bool
	// is the cache initialized?
	initialized  atm.Bool
	// maps NUMA affinity and device index to a response
	resmgmtpb    map[int]map[int][]byte
	// maps NUMA affinity to a device index
	devIdx       map[int]int
	mutex        sync.Mutex
}

// loadBalance is a simple round-robin load balancing scheme
// to assign network interface adapters to clients
// on the same NUMA node that have multiple adapters
// to choose from.  Returns the index of the device to use.
func (aic *attachInfoCache) loadBalance(numaNode int) int {
	aic.mutex.Lock()
	numDevs := len(aic.resmgmtpb[numaNode])
	// keep the deviceIndex in range of the current resmgmtpb map
	deviceIndex := aic.devIdx[numaNode] % numDevs
	aic.devIdx[numaNode] = (deviceIndex + 1) % numDevs
	aic.mutex.Unlock()
	return deviceIndex
}

func (aic *attachInfoCache) getResponse(numaNode int) ([]byte, error) {
	deviceIndex := aic.loadBalance(numaNode)
	resmgmtpb, ok := aic.resmgmtpb[numaNode][deviceIndex]
	if !ok {
		return nil, errors.Errorf("GetAttachInfo entry for numaNode %d device index %d did not exist", numaNode, deviceIndex)
	}
	aic.log.Debugf("Retrieved response for NUMA %d with device index %d\n", numaNode, deviceIndex)
	return resmgmtpb, nil
}

// initResponseCache generates a unique dRPC response corresponding to each device specified
// in the scanResults.  The responses are differentiated based on the network device NUMA affinity.
func (aic *attachInfoCache) initResponseCache(resp *mgmtpb.GetAttachInfoResp, scanResults []netdetect.FabricScan) error {
	aic.mutex.Lock()
	defer aic.mutex.Unlock()

	if len(scanResults) == 0 {
		return errors.Errorf("No devices found in the scanResults")
	}

	// Make a new map each time the cache is initialized
	aic.resmgmtpb = make(map[int]map[int][]byte)

	// Make a new map just once.
	// Preserve any previous device index map in order to maintain ability to load balance
	if len(aic.devIdx) == 0 {
		aic.devIdx = make(map[int]int)
	}

	for _, fs := range scanResults {
		if fs.DeviceName == "lo" {
			continue
		}
		resp.Interface = fs.DeviceName
		// by default, the domain is the deviceName
		resp.Domain = fs.DeviceName
		if strings.HasPrefix(resp.Provider, "ofi+verbs") {
			deviceAlias, err := netdetect.GetDeviceAlias(resp.Interface)
			if err != nil {
				aic.log.Debugf("non-fatal error: %v. unable to determine OFI_DOMAIN for %s", err, resp.Interface)
			} else {
				resp.Domain = deviceAlias
				aic.log.Debugf("OFI_DOMAIN has been detected as: %s", resp.Domain)
			}
		}
		numa := int(fs.NUMANode)

		resmgmtpb, err := proto.Marshal(resp)
		if err != nil {
			return drpc.MarshalingFailure()
		}

		_, ok := aic.resmgmtpb[numa]
		if !ok {
			aic.resmgmtpb[numa] = make(map[int][]byte)
		}
		aic.resmgmtpb[numa][len(aic.resmgmtpb[numa])] = resmgmtpb
		aic.log.Debugf("Added device %s, domain %s for NUMA %d, device number %d\n", resp.Interface, resp.Domain, numa, len(aic.resmgmtpb[numa])-1)
	}

	// If there are any entries in the cache and caching is enabled, the cache is 'initialized'
	if len(aic.resmgmtpb) > 0 && aic.enabled.IsTrue() {
		aic.initialized.SetTrue()
	}

	return nil
}
