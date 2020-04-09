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
	"fmt"
	"net"
	"os"
	"strings"
	"sync"
	"syscall"
	"time"

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

type clientProcessCfg struct {
	timeStamp time.Time
	numaNode  uint
	devIdx    int
}

// mgmtModule represents the daos_agent dRPC module. It acts mostly as a
// Management Service proxy, handling dRPCs sent by libdaos by forwarding them
// to MS.
type mgmtModule struct {
	log logging.Logger
	sys string
	// The access point
	ap               string
	tcfg             *security.TransportConfig
	cachedAttachInfo bool
	// maps NUMA affinity and device index to a response
	resmgmtpb map[uint]map[int][]byte
	// maps NUMA affinity to a device index
	devIdx     map[uint]int
	clientData map[int32]clientProcessCfg
	mutex      sync.Mutex
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
func (mod *mgmtModule) loadBalance(numaNode uint) {
	numDevs := len(mod.resmgmtpb[numaNode])
	newDevIdx := (mod.devIdx[numaNode] + 1) % numDevs
	mod.devIdx[numaNode] = newDevIdx
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
	mod.mutex.Lock()
	defer mod.mutex.Unlock()

	if os.Getenv("DAOS_AGENT_DISABLE_CACHE") == "true" {
		mod.cachedAttachInfo = false
		mod.log.Debugf("GetAttachInfo agent caching has been disabled\n")
	}

	fi, err := os.Stat(fmt.Sprintf("/proc/%d", pid))
	if err != nil {
		return nil, errors.Wrapf(err, "client process %d not found", pid)
	}

	if mod.cachedAttachInfo {
		clientData := mod.clientData[pid]
		// If it's a client with new PID, or a different client with a cached PID,
		// update the cache with the new data.
		//if clientData.timeStamp != fi.ModTime().String() {
		if clientData.timeStamp != fi.ModTime() {
			numaNode, err := netdetect.GetNUMASocketIDForPid(pid)
			if err != nil {
				return nil, err
			}

			mod.clientData[pid] = clientProcessCfg{devIdx: mod.devIdx[numaNode], timeStamp: fi.ModTime(), numaNode: numaNode}
			resmgmtpb, ok := mod.resmgmtpb[numaNode][mod.devIdx[numaNode]]
			if !ok {
				return nil, errors.Errorf("GetAttachInfo entry for numaNode %d device index %d did not exist", numaNode, clientData.devIdx)
			}
			mod.log.Debugf("Client on NUMA %d using device %d\n", numaNode, mod.devIdx[numaNode])
			mod.loadBalance(numaNode)
			return resmgmtpb, nil
		}
		// Otherwise, return the cached data for this client
		mod.log.Debugf("Client on NUMA %d using device %d\n", clientData.numaNode, clientData.devIdx)
		resmgmtpb, ok := mod.resmgmtpb[clientData.numaNode][clientData.devIdx]
		if !ok {
			return nil, errors.Errorf("GetAttachInfo entry for numaNode %d device index %d did not exist", clientData.numaNode, clientData.devIdx)
		}
		return resmgmtpb, nil
	}

	numaNode, err := netdetect.GetNUMASocketIDForPid(pid)
	if err != nil {
		return nil, err
	}

	if len(mod.clientData) == 0 {
		mod.clientData = make(map[int32]clientProcessCfg)
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

	// Create and cache a set of dRPC responses that can be indexed by the NUMA affinity and the device index.
	// The NUMA affinity depends on the client PID.  The device index depends on an internally maintained
	// index that points to the current device to offer a client on a given NUMA node.
	if len(mod.resmgmtpb) == 0 {
		mod.resmgmtpb = make(map[uint]map[int][]byte)

		if len(mod.devIdx) == 0 {
			mod.devIdx = make(map[uint]int)
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
					mod.log.Debugf("non-fatal error: %v. unable to determine OFI_DOMAIN for %s", err, resp.Interface)
				} else {
					resp.Domain = deviceAlias
					mod.log.Debugf("OFI_DOMAIN has been detected as: %s", resp.Domain)
				}
			}

			resmgmtpb, err := proto.Marshal(resp)
			if err != nil {
				return nil, drpc.MarshalingFailure()
			}

			_, ok := mod.resmgmtpb[fs.NUMANode]
			if !ok {
				mod.resmgmtpb[fs.NUMANode] = make(map[int][]byte)
			}
			mod.resmgmtpb[fs.NUMANode][len(mod.resmgmtpb[fs.NUMANode])] = resmgmtpb
			mod.log.Debugf("Added device %s, domain %s for NUMA %d, device number %d\n", resp.Interface, resp.Domain, fs.NUMANode, len(mod.resmgmtpb[fs.NUMANode]))
		}

		for i, numaEntry := range mod.resmgmtpb {
			mod.log.Debugf("There are %d device entries for NUMA node %d", len(numaEntry), i)
		}
	}

	mod.clientData[pid] = clientProcessCfg{devIdx: mod.devIdx[numaNode], timeStamp: fi.ModTime(), numaNode: numaNode}
	resmgmtpb, ok := mod.resmgmtpb[numaNode][mod.clientData[pid].devIdx]
	if !ok {
		return nil, errors.Errorf("GetAttachInfo entry for numaNode %d and device index %d did not exist.", numaNode, mod.clientData[pid].devIdx)
	}
	mod.loadBalance(numaNode)
	mod.cachedAttachInfo = true

	return resmgmtpb, nil
}
