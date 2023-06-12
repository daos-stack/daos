//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"net"
	"sync"

	"github.com/pkg/errors"
	"golang.org/x/net/context"
	"golang.org/x/sys/unix"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common"
	pblog "github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
)

// mgmtModule represents the daos_agent dRPC module. It acts mostly as a
// Management Service proxy, handling dRPCs sent by libdaos by forwarding them
// to MS.
type mgmtModule struct {
	attachInfoMutex sync.RWMutex
	fabricMutex     sync.RWMutex

	log            logging.Logger
	sys            string
	ctlInvoker     control.Invoker
	cache          *InfoCache
	monitor        *procMon
	useDefaultNUMA bool

	numaGetter hardware.ProcessNUMAProvider
}

func (mod *mgmtModule) HandleCall(ctx context.Context, session *drpc.Session, method drpc.Method, req []byte) ([]byte, error) {
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

	if agentIsShuttingDown(ctx) {
		mod.log.Errorf("agent is shutting down, dropping %s", method)
		return nil, drpc.NewFailureWithMessage("agent is shutting down")
	}

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

	client := &procInfo{
		pid: pid,
	}
	procName, err := common.GetProcName(int(pid))
	if err == nil {
		client.name = procName
	}
	mod.log.Tracef("%s: %s", client, pblog.Debug(pbReq))

	// Check the system name. Due to the special daos_init-dc_mgmt_net_cfg
	// case, where the system name is not available, we let an empty
	// system name indicates such, and hence skip the check.
	if pbReq.Sys != "" && pbReq.Sys != mod.sys {
		mod.log.Errorf("%s: %s: unknown system name", client, pbReq.Sys)
		respb, err := proto.Marshal(&mgmtpb.GetAttachInfoResp{Status: int32(daos.InvalidInput)})
		if err != nil {
			return nil, drpc.MarshalingFailure()
		}
		return respb, err
	}

	numaNode, err := mod.getNUMANode(ctx, pid)
	if err != nil {
		mod.log.Errorf("%s: unable to get NUMA node: %s", client, err)
		return nil, err
	}
	mod.log.Tracef("%s: detected numa %d", client, numaNode)

	resp, err := mod.getAttachInfo(ctx, int(numaNode), pbReq.Sys)
	switch {
	case fault.IsFaultCode(err, code.ServerWrongSystem):
		resp = &mgmtpb.GetAttachInfoResp{Status: int32(daos.ControlIncompatible)}
	case fault.IsFaultCode(err, code.SecurityInvalidCert):
		resp = &mgmtpb.GetAttachInfoResp{Status: int32(daos.BadCert)}
	case control.IsMSConnectionFailure(err):
		resp = &mgmtpb.GetAttachInfoResp{Status: int32(daos.Unreachable)}
	case err != nil:
		return nil, err
	}

	if resp.ClientNetHint != nil {
		mod.log.Infof("%s: numa:%d iface:%s dom:%s prov:%s srx:%d", client, numaNode,
			resp.ClientNetHint.Interface, resp.ClientNetHint.Domain,
			resp.ClientNetHint.Provider, resp.ClientNetHint.SrvSrxSet)
	}
	mod.log.Tracef("%s: %s", client, pblog.Debug(resp))
	return proto.Marshal(resp)
}

func (mod *mgmtModule) getNUMANode(ctx context.Context, pid int32) (uint, error) {
	if mod.useDefaultNUMA {
		return 0, nil
	}

	numaNode, err := mod.numaGetter.GetNUMANodeIDForPID(ctx, pid)
	if errors.Is(err, hardware.ErrNoNUMANodes) {
		mod.log.Debug("system is not NUMA-aware")
		mod.useDefaultNUMA = true
		return 0, nil
	} else if err != nil {
		return 0, errors.Wrapf(err, "failed to get NUMA node ID for pid %d", pid)
	}

	return numaNode, nil
}

func (mod *mgmtModule) getAttachInfo(ctx context.Context, numaNode int, sys string) (*mgmtpb.GetAttachInfoResp, error) {
	resp, err := mod.getAttachInfoResp(ctx, sys)
	if err != nil {
		mod.log.Errorf("failed to fetch AttachInfo: %s", err.Error())
		return nil, err
	}

	fabricIF, err := mod.getFabricInterface(ctx, numaNode, hardware.NetDevClass(resp.ClientNetHint.NetDevClass), resp.ClientNetHint.Provider)
	if err != nil {
		mod.log.Errorf("failed to fetch fabric interface of type %s: %s",
			hardware.NetDevClass(resp.ClientNetHint.NetDevClass), err.Error())
		return nil, err
	}

	resp.ClientNetHint.Interface = fabricIF.Name
	resp.ClientNetHint.Domain = fabricIF.Name
	if fabricIF.Domain != "" {
		resp.ClientNetHint.Domain = fabricIF.Domain
		mod.log.Tracef("device %s: OFI_DOMAIN detected as: %s",
			resp.ClientNetHint.Interface, resp.ClientNetHint.Domain)
	}

	return resp, nil
}

func (mod *mgmtModule) getAttachInfoResp(ctx context.Context, sys string) (*mgmtpb.GetAttachInfoResp, error) {
	ctlResp, err := mod.cache.GetAttachInfo(ctx, sys)
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.GetAttachInfoResp)
	if err := convert.Types(ctlResp, resp); err != nil {
		return nil, err
	}
	return resp, nil
}

func (mod *mgmtModule) getFabricInterface(ctx context.Context, numaNode int, netDevClass hardware.NetDevClass, provider string) (*FabricInterface, error) {
	return mod.cache.GetFabricDevice(ctx, numaNode, netDevClass, provider)
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

// RefreshCache triggers a refresh of all data that is currently cached. If nothing has been cached
// yet, it does nothing.
func (mod *mgmtModule) RefreshCache(ctx context.Context) error {
	return mod.cache.Refresh(ctx)
}
