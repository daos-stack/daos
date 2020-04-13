//
// (C) Copyright 2019-2020 Intel Corporation.
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
	"net"
	"os"
	"strings"
	"sync"
	"syscall"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"golang.org/x/net/context"
	"google.golang.org/grpc"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
)

type attachInfoCache struct {
	log              logging.Logger
	cachedAttachInfo bool
	// maps NUMA affinity and device index to a response
	resmgmtpb map[int]map[int][]byte
	// maps NUMA affinity to a device index
	devIdx map[int]int
	mutex  sync.Mutex
}

// mgmtModule represents the daos_agent dRPC module. It acts mostly as a
// Management Service proxy, handling dRPCs sent by libdaos by forwarding them
// to MS.
type mgmtModule struct {
	log logging.Logger
	sys string
	// The access point
	ap             string
	tcfg           *security.TransportConfig
	attachInfoResp attachInfoCache
}

func (mod *mgmtModule) HandleCall(session *drpc.Session, method int32, req []byte) ([]byte, error) {
	switch method {
	case drpc.MethodGetAttachInfo:

		uc, ok := session.Conn.(*net.UnixConn)
		if !ok {
			return nil, errors.Errorf("session.Conn type conversion failed")
		}

		file, err := uc.File()
		if err != nil {
			return nil, err
		}
		defer file.Close()

		fd := int(file.Fd())
		cred, err := syscall.GetsockoptUcred(fd, syscall.SOL_SOCKET, syscall.SO_PEERCRED)
		if err != nil {
			return nil, err
		}

		return mod.handleGetAttachInfo(req, cred.Pid)
	default:
		return nil, drpc.UnknownMethodFailure()
	}
}

func (mod *mgmtModule) ID() int32 {
	return drpc.ModuleMgmt
}

// loadBalance is a simple round-robin load balancing scheme
// to assign network interface adapters to clients
// on the same NUMA node that have multiple adapters
// to choose from.
func (aic *attachInfoCache) loadBalance(numaNode int) {
	numDevs := len(aic.resmgmtpb[numaNode])
	newDevIdx := (aic.devIdx[numaNode] + 1) % numDevs
	aic.devIdx[numaNode] = newDevIdx
}

func (aic *attachInfoCache) getCachedResponse(numaNode int) ([]byte, error) {
	deviceIndex := aic.devIdx[numaNode]

	aic.loadBalance(numaNode)

	resmgmtpb, ok := aic.resmgmtpb[numaNode][deviceIndex]
	if !ok {
		return nil, errors.Errorf("Cached GetAttachInfo entry for numaNode %d device index %d did not exist", numaNode, deviceIndex)
	}
	aic.log.Debugf("Retrieved cached response for NUMA %d with device index %d\n", numaNode, deviceIndex)
	return resmgmtpb, nil
}

// initResponseCache generates a unique dRPC response corresponding to each device specified
// in the scanResults.  The responses are differentiated based on the network device NUMA affinity.
func (aic *attachInfoCache) initResponseCache(resp *mgmtpb.GetAttachInfoResp, scanResults []netdetect.FabricScan) error {
	if len(scanResults) == 0 {
		return errors.Errorf("No devices found in the scanResults")
	}

	if len(aic.resmgmtpb) == 0 {
		aic.resmgmtpb = make(map[int]map[int][]byte)
	}

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

	// As long as there are some responses in the cache, enable it.
	if len(aic.resmgmtpb) > 0 {
		aic.enableCache()
	}

	return nil
}

func (aic *attachInfoCache) lock() {
	aic.mutex.Lock()
}

func (aic *attachInfoCache) unlock() {
	aic.mutex.Unlock()
}

func (aic *attachInfoCache) haveCachedData() bool {
	return aic.cachedAttachInfo
}

func (aic *attachInfoCache) disableCache() {
	aic.cachedAttachInfo = false
}

func (aic *attachInfoCache) enableCache() {
	aic.cachedAttachInfo = true
}

func (aic *attachInfoCache) setLogger(log logging.Logger) {
	aic.log = log
}

// handleGetAttachInfo invokes the GetAttachInfo dRPC.  The agent determines the
// NUMA node for the client process based on its PID.  Then based on the
// server's provider, chooses a matching network interface and domain from the
// client machine that has the same NUMA affinity.  It is considered an error if
// the client application is bound to a NUMA node that does not have a network
// device / provider combination with the same NUMA affinity.
//
// The client machine may have more than one matching network interface per
// NUMA node.  In order to load balance the client application's use of multiple
// network interfaces on a given NUMA node, a round robin resource allocation
// scheme is used to choose the next device for that node.  See "loadBalance()"
// for details.
//
// The agent caches the local device data and all possible responses the first
// time this dRPC is invoked. Subsequent calls receive the cached data.
// The use of cached data may be disabled by exporting
// "DAOS_AGENT_DISABLE_CACHE=true" in the environment running the daos_agent.
func (mod *mgmtModule) handleGetAttachInfo(reqb []byte, pid int32) ([]byte, error) {
	mod.attachInfoResp.lock()
	defer mod.attachInfoResp.unlock()

	mod.attachInfoResp.setLogger(mod.log)

	if os.Getenv("DAOS_AGENT_DISABLE_CACHE") == "true" {
		mod.attachInfoResp.disableCache()
		mod.log.Debugf("GetAttachInfo agent caching has been disabled\n")
	}

	if mod.attachInfoResp.haveCachedData() {
		numaNode, err := netdetect.GetNUMASocketIDForPid(pid)
		if err != nil {
			mod.log.Debugf("Could not determine the NUMA node affinity.  Using numaNode 0")
			numaNode = 0
		}

		resmgmtpb, err := mod.attachInfoResp.getCachedResponse(numaNode)
		if err != nil {
			return nil, err
		}
		return resmgmtpb, nil
	}

	req := &mgmtpb.GetAttachInfoReq{}
	if err := proto.Unmarshal(reqb, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}

	mod.log.Debugf("GetAttachInfo %s %v", mod.ap, *req)

	if req.Sys != mod.sys {
		return nil, errors.Errorf("unknown system name %s", req.Sys)
	}

	dialOpt, err := security.DialOptionForTransportConfig(mod.tcfg)
	if err != nil {
		return nil, err
	}
	opts := []grpc.DialOption{dialOpt}

	conn, err := grpc.Dial(mod.ap, opts...)
	if err != nil {
		return nil, errors.Wrapf(err, "dial %s", mod.ap)
	}
	defer conn.Close()

	client := mgmtpb.NewMgmtSvcClient(conn)

	resp, err := client.GetAttachInfo(context.Background(), req)
	if err != nil {
		return nil, errors.Wrapf(err, "GetAttachInfo %s %v", mod.ap, *req)
	}

	if resp.Provider == "" {
		return nil, errors.Errorf("GetAttachInfo %s %v contained no provider.", mod.ap, *req)
	}

	// Scan the local fabric to determine what devices are available that match our provider
	scanResults, err := netdetect.ScanFabric(resp.Provider)
	if err != nil {
		return nil, err
	}

	err = mod.attachInfoResp.initResponseCache(resp, scanResults)
	if err != nil {
		return nil, err
	}

	numaNode, err := netdetect.GetNUMASocketIDForPid(pid)
	if err != nil {
		mod.log.Debugf("Could not determine the NUMA node affinity.  Using numaNode 0")
		numaNode = 0
	}

	resmgmtpb, err := mod.attachInfoResp.getCachedResponse(numaNode)
	if err != nil {
		return nil, err
	}

	return resmgmtpb, nil
}
