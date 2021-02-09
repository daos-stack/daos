//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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
	case drpc.MethodNotifyPoolConnect:
		return nil, mod.handleNotifyPoolConnect(ctx, req, cred.Pid)
	case drpc.MethodNotifyPoolDisconnect:
		return nil, mod.handleNotifyPoolDisconnect(ctx, req, cred.Pid)
	case drpc.MethodNotifyExit:
		// There isn't anything we can do here if this fails so just
		// call the disconnect handler and return success.
		mod.handleNotifyExit(ctx, cred.Pid)
		return nil, nil
	default:
		return nil, drpc.UnknownMethodFailure()
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
	pbReq := new(mgmtpb.GetAttachInfoReq)
	if err := proto.Unmarshal(reqb, pbReq); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}

	mod.log.Debugf("GetAttachInfo req from client: %+v", pbReq)

	// Check the system name. Due to the special daos_init-dc_mgmt_net_cfg
	// case, where the system name is not available, we let an empty
	// system name indicates such, and hence skip the check.
	if pbReq.Sys != "" && pbReq.Sys != mod.sys {
		mod.log.Errorf("GetAttachInfo: %s: unknown system name", pbReq.Sys)
		respb, err := proto.Marshal(&mgmtpb.GetAttachInfoResp{Status: int32(drpc.DaosInvalidInput)})
		if err != nil {
			return nil, drpc.MarshalingFailure()
		}
		return respb, err
	}

	var err error
	var numaNode int

	if mod.numaAware {
		numaNode, err = netdetect.GetNUMASocketIDForPid(mod.netCtx, pid)
		if err != nil {
			return nil, err
		}
	}

	// synchronize access to mod.aiCache.* resources used below
	mod.mutex.Lock()
	defer mod.mutex.Unlock()

	if mod.aiCache.isCached() {
		if !mod.numaAware {
			numaNode = mod.aiCache.defaultNumaNode
		}
		return mod.aiCache.getResponse(numaNode)
	}

	// Ask the MS for _all_ info, regardless of pbReq.AllRanks, so that the
	// cache can serve future "pbReq.AllRanks == true" requests.
	req := new(control.GetAttachInfoReq)
	req.SetSystem(pbReq.GetSys())
	req.AllRanks = true
	resp, err := control.GetAttachInfo(ctx, mod.ctlInvoker, req)
	if err != nil {
		return nil, errors.Wrapf(err, "GetAttachInfo %+v", pbReq)
	}

	if resp.Provider == "" {
		return nil, errors.New("GetAttachInfo response contained no provider")
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

	if !mod.numaAware {
		numaNode = mod.aiCache.defaultNumaNode
	}

	cacheResp, err := mod.aiCache.getResponse(numaNode)
	if err != nil {
		return nil, err
	}

	// If pbReq.AllRanks == false, we shouldn't return the rank URIs.
	// Implementing that may require changing the cache to either hold
	// unmarshalled responses (more computation work for daos_agent) or
	// two variants of marshalled responses.

	return cacheResp, err
}

func (mod *mgmtModule) handleNotifyPoolConnect(ctx context.Context, reqb []byte, pid int32) error {
	pbReq := new(mgmtpb.PoolMonitorReq)
	if err := proto.Unmarshal(reqb, pbReq); err != nil {
		return drpc.UnmarshalingPayloadFailure()
	}
	mod.monitor.AddPoolHandle(ctx, pid, pbReq)
	return nil
}

func (mod *mgmtModule) handleNotifyPoolDisconnect(ctx context.Context, reqb []byte, pid int32) error {
	pbReq := new(mgmtpb.PoolMonitorReq)
	if err := proto.Unmarshal(reqb, pbReq); err != nil {
		return drpc.UnmarshalingPayloadFailure()
	}
	mod.monitor.RemovePoolHandle(ctx, pid, pbReq)
	return nil
}

// handleNotifyExit crafts a new request for the process monitor to inform the
// monitor that a process is exiting. Even though the process is terminating
// cleanly disconnect will inform the control plane of any outstanding handles
// that the process held open.
func (mod *mgmtModule) handleNotifyExit(ctx context.Context, pid int32) {
	mod.monitor.NotifyExit(ctx, pid)
}
