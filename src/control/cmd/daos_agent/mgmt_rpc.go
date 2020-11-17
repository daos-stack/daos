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
	"sync"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"golang.org/x/net/context"
	"golang.org/x/sys/unix"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
)

// mgmtModule represents the daos_agent dRPC module. It acts mostly as a
// Management Service proxy, handling dRPCs sent by libdaos by forwarding them
// to MS.
type mgmtModule struct {
	log        logging.Logger
	sys        string
	ctlInvoker control.Invoker
	aiCache    *attachInfoCache
	numaAware  bool
	netCtx     context.Context
	mutex      sync.Mutex
	monitor    *procMon
}

func (mod *mgmtModule) HandleCall(session *drpc.Session, method drpc.Method, req []byte) ([]byte, error) {
	if method != drpc.MethodGetAttachInfo && method != drpc.MethodDisconnect {
		return nil, drpc.UnknownMethodFailure()
	}

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
	cred, err := unix.GetsockoptUcred(fd, unix.SOL_SOCKET, unix.SO_PEERCRED)
	if err != nil {
		return nil, err
	}

	ctx := context.TODO() // FIXME: Should be the top-level context.

	switch method {
	case drpc.MethodGetAttachInfo:
		return mod.handleGetAttachInfo(ctx, req, cred.Pid)
	case drpc.MethodDisconnect:
		// There isn't anything we can do here if this fails so just
		// call the disconnect handler and return success.
		mod.handleDisconnect(ctx, cred.Pid)
		return nil, nil
	}

	return nil, drpc.UnknownMethodFailure()
}

func (mod *mgmtModule) ID() drpc.ModuleID {
	return drpc.ModuleMgmt
}

// handleGetAttachInfo invokes the GetAttachInfo dRPC.  The agent determines the
// NUMA node for the client process based on its PID.  Then based on the
// server's provider, chooses a matching network interface and domain from the
// client machine that has the same NUMA affinity.  It is considered an error if
// the client application is bound to a NUMA node that does not have a network
// device / provider combination with the same NUMA affinity.
//
// The agent caches the local device data and all possible responses the first
// time this dRPC is invoked. Subsequent calls receive the cached data.
// The use of cached data may be disabled by exporting
// "DAOS_AGENT_DISABLE_CACHE=true" in the environment running the daos_agent.
func (mod *mgmtModule) handleGetAttachInfo(ctx context.Context, reqb []byte, pid int32) ([]byte, error) {
	var err error
	numaNode := mod.aiCache.defaultNumaNode

	if mod.numaAware {
		numaNode, err = netdetect.GetNUMASocketIDForPid(mod.netCtx, pid)
		if err != nil {
			return nil, err
		}
	}

	// The flow is optimized for the case where isCached() is true.  In the normal case,
	// caching is enabled, there's data in the info cache and the agent can quickly return
	// a response without the overhead of a mutex.
	if mod.aiCache.isCached() {
		mod.monitor.RegisterProcess(ctx, pid)
		return mod.aiCache.getResponse(numaNode)
	}

	// If the cache was not initialized, protect cache initialization
	// and check the initialization status once the mutex is obtained.
	mod.mutex.Lock()
	defer mod.mutex.Unlock()

	// If another thread succeeded in initializing the cache while this thread waited
	// to get the mutex, return the cached response instead of initializing the cache again.
	if mod.aiCache.isCached() {
		mod.monitor.RegisterProcess(ctx, pid)
		return mod.aiCache.getResponse(numaNode)
	}

	pbReq := new(mgmtpb.GetAttachInfoReq)
	if err := proto.Unmarshal(reqb, pbReq); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}

	mod.log.Debugf("GetAttachInfo req from client: %+v", pbReq)

	if pbReq.Sys != mod.sys {
		return nil, errors.Errorf("unknown system name %s", pbReq.Sys)
	}

	resp, err := control.GetAttachInfo(ctx, mod.ctlInvoker, &control.GetAttachInfoReq{
		System: pbReq.Sys,
	})
	if err != nil {
		return nil, errors.Wrapf(err, "GetAttachInfo %+v", pbReq)
	}

	if resp.Provider == "" {
		return nil, errors.New("GetAttachInfo response contained no provider.")
	}

	// Scan the local fabric to determine what devices are available that match our provider
	scanResults, err := netdetect.ScanFabric(mod.netCtx, resp.Provider)
	if err != nil {
		return nil, err
	}

	mod.log.Debugf("GetAttachInfo resp from MS: %+v", resp)

	pbResp := new(mgmtpb.GetAttachInfoResp)
	if err := convert.Types(resp, pbResp); err != nil {
		return nil, errors.Wrap(err, "Failed to convert GetAttachInfo response")
	}

	err = mod.aiCache.initResponseCache(mod.netCtx, pbResp, scanResults)
	if err != nil {
		return nil, err
	}

	cacheResp, err := mod.aiCache.getResponse(numaNode)
	if err != nil {
		return nil, err
	}

	mod.monitor.RegisterProcess(ctx, pid)

	return cacheResp, err
}

// handleDisconnect crafts a new request for the process monitor to inform the
// monitor that a process is exiting. Even though the process is terminating
// cleanly disconnect will inform the control plane of any outstanding handles
// that the process held open.
func (mod *mgmtModule) handleDisconnect(ctx context.Context, pid int32) {
	mod.monitor.UnregisterProcess(ctx, pid)
}
