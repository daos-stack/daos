//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"net"
	"sync"
	"time"

	"github.com/pkg/errors"
	"golang.org/x/net/context"
	"golang.org/x/sys/unix"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
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

	log            logging.Logger
	sys            string
	ctlInvoker     control.Invoker
	attachInfo     *attachInfoCache
	fabricInfo     *localFabricCache
	monitor        *procMon
	useDefaultNUMA bool

	numaGetter     hardware.ProcessNUMAProvider
	provider       string
	devClassGetter hardware.NetDevClassProvider
	devStateGetter hardware.NetDevStateProvider
	fabricScanner  *hardware.FabricScanner
	netIfaces      func() ([]net.Interface, error)
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

	mod.log.Debugf("GetAttachInfo req from client: %+v", pbReq)

	// Check the system name. Due to the special daos_init-dc_mgmt_net_cfg
	// case, where the system name is not available, we let an empty
	// system name indicates such, and hence skip the check.
	if pbReq.Sys != "" && pbReq.Sys != mod.sys {
		mod.log.Errorf("GetAttachInfo: %s: unknown system name", pbReq.Sys)
		respb, err := proto.Marshal(&mgmtpb.GetAttachInfoResp{Status: int32(daos.InvalidInput)})
		if err != nil {
			return nil, drpc.MarshalingFailure()
		}
		return respb, err
	}

	numaNode, err := mod.getNUMANode(ctx, pid)
	if err != nil {
		mod.log.Errorf("unable to get NUMA node: %s", err.Error())
		return nil, err
	}

	mod.log.Debugf("client process NUMA node %d", numaNode)

	resp, err := mod.getAttachInfo(ctx, int(numaNode), pbReq)
	if err != nil {
		return nil, err
	}

	mod.log.Debugf("GetAttachInfoResp: %+v", resp)

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
		return 0, errors.Wrap(err, "get NUMA node ID")
	}

	return numaNode, nil
}

func (mod *mgmtModule) getAttachInfo(ctx context.Context, numaNode int, req *mgmtpb.GetAttachInfoReq) (*mgmtpb.GetAttachInfoResp, error) {
	rawResp, err := mod.getAttachInfoResp(ctx, numaNode, req.Sys)
	if err != nil {
		mod.log.Errorf("failed to fetch remote AttachInfo: %s", err.Error())
		return nil, err
	}

	reqProviders := mod.getInterfaceProviders(req.Interface, req.Domain)

	resp, err := mod.selectAttachInfo(rawResp, reqProviders)
	if err != nil {
		return nil, err
	}

	// Requested fabric interface/domain behave as a simple override. If we weren't able to
	// validate them, we return them to the user with the understanding that perhaps the user
	// knows what they're doing.
	iface := req.Interface
	domain := req.Domain
	if req.Interface == "" {
		fabricIF, err := mod.getFabricInterface(ctx, &FabricIfaceParams{
			NUMANode: numaNode,
			DevClass: hardware.NetDevClass(resp.ClientNetHint.NetDevClass),
			Provider: resp.ClientNetHint.Provider,
		})
		if err != nil {
			mod.log.Errorf("failed to fetch fabric interface of type %s: %s",
				hardware.NetDevClass(resp.ClientNetHint.NetDevClass), err.Error())
			return nil, err
		}

		iface = fabricIF.Name
		domain = fabricIF.Domain
	}

	resp.ClientNetHint.Interface = iface
	resp.ClientNetHint.Domain = iface
	if domain != "" {
		resp.ClientNetHint.Domain = domain
		mod.log.Debugf("OFI_DOMAIN for %s has been detected as: %s",
			resp.ClientNetHint.Interface, resp.ClientNetHint.Domain)
	}

	return resp, nil
}

func (mod *mgmtModule) getAttachInfoResp(ctx context.Context, numaNode int, sys string) (*mgmtpb.GetAttachInfoResp, error) {
	return mod.attachInfo.Get(ctx, numaNode, sys, mod.getAttachInfoRemote)
}

func (mod *mgmtModule) getInterfaceProviders(iface, domain string) common.StringSet {
	if iface == "" {
		return nil
	}

	if domain == "" {
		domain = iface
	}

	fis, err := mod.fabricInfo.localNUMAFabric.FindDevice(&FabricIfaceParams{
		Interface: iface,
		Domain:    domain,
	})
	if err != nil {
		mod.log.Errorf("client-requested fabric interface/domain not detected: %s", err.Error())
		mod.log.Error("communications on this interface may fail")
		return nil
	}

	providers := common.NewStringSet()
	for _, fi := range fis {
		providers.AddUnique(fi.Providers()...)
	}
	return providers
}

func (mod *mgmtModule) selectAttachInfo(srvResp *mgmtpb.GetAttachInfoResp, reqProviders common.StringSet) (*mgmtpb.GetAttachInfoResp, error) {
	providers := reqProviders
	if mod.provider != "" {
		if len(reqProviders) > 0 && !reqProviders.Has(mod.provider) {
			mod.log.Errorf("configured provider %q not included in requested interface's detected providers: %s", reqProviders)
			mod.log.Error("communications on this interface may fail")
		}
		providers = common.NewStringSet(mod.provider)
	}

	if len(providers) == 0 {
		return srvResp, nil
	}

	if providers.Has(srvResp.ClientNetHint.Provider) {
		return srvResp, nil
	}

	for _, hint := range srvResp.SecondaryClientNetHints {
		if providers.Has(hint.Provider) {
			uris, err := mod.getProviderURIs(srvResp, hint.Provider)
			if err == nil {
				return &mgmtpb.GetAttachInfoResp{
					Status:        srvResp.Status,
					RankUris:      uris,
					MsRanks:       srvResp.MsRanks,
					ClientNetHint: hint,
				}, nil
			}
		}
	}

	return nil, errors.Errorf("no valid connection information for providers: %s", providers)
}

func (mod *mgmtModule) getProviderURIs(srvResp *mgmtpb.GetAttachInfoResp, provider string) ([]*mgmtpb.GetAttachInfoResp_RankUri, error) {
	uris := []*mgmtpb.GetAttachInfoResp_RankUri{}
	for _, uri := range srvResp.SecondaryRankUris {
		if uri.Provider == provider {
			uris = append(uris, uri)
		}
	}

	if len(uris) == 0 {
		return nil, errors.Errorf("no rank URIs for provider %q", provider)
	}

	return uris, nil
}

func (mod *mgmtModule) getAttachInfoRemote(ctx context.Context, numaNode int, sys string) (*mgmtpb.GetAttachInfoResp, error) {
	// Ask the MS for _all_ info, regardless of pbReq.AllRanks, so that the
	// cache can serve future "pbReq.AllRanks == true" requests.
	req := new(control.GetAttachInfoReq)
	req.SetSystem(sys)
	req.AllRanks = true
	resp, err := control.GetAttachInfo(ctx, mod.ctlInvoker, req)
	if err != nil {
		return nil, errors.Wrapf(err, "GetAttachInfo %+v", req)
	}

	if resp.ClientNetHint.Provider == "" {
		return nil, errors.New("GetAttachInfo response contained no provider")
	}

	pbResp := new(mgmtpb.GetAttachInfoResp)
	if err := convert.Types(resp, pbResp); err != nil {
		return nil, errors.Wrap(err, "Failed to convert GetAttachInfo response")
	}

	return pbResp, nil
}

func (mod *mgmtModule) getFabricInterface(ctx context.Context, params *FabricIfaceParams) (*FabricInterface, error) {
	mod.attachInfoMutex.Lock()
	defer mod.attachInfoMutex.Unlock()

	if mod.fabricInfo.IsCached() {
		return mod.getCachedInterface(ctx, params)
	}

	if err := mod.waitFabricReady(ctx, params.DevClass); err != nil {
		return nil, err
	}

	result, err := mod.fabricScanner.Scan(ctx)
	if err != nil {
		return nil, err
	}

	mod.fabricInfo.CacheScan(ctx, result)

	return mod.getCachedInterface(ctx, params)
}

func (mod *mgmtModule) getCachedInterface(ctx context.Context, params *FabricIfaceParams) (*FabricInterface, error) {
	if params.Interface != "" {
		fi, err := mod.fabricInfo.localNUMAFabric.FindDevice(params)
		if err != nil {
			return nil, err
		}
		return fi[0], nil
	}
	return mod.fabricInfo.GetDevice(params)
}

func (mod *mgmtModule) waitFabricReady(ctx context.Context, netDevClass hardware.NetDevClass) error {
	if mod.netIfaces == nil {
		mod.netIfaces = net.Interfaces
	}
	ifaces, err := mod.netIfaces()
	if err != nil {
		return err
	}

	var needIfaces []string
	for _, iface := range ifaces {
		devClass, err := mod.devClassGetter.GetNetDevClass(iface.Name)
		if err != nil {
			return err
		}
		if devClass == netDevClass {
			needIfaces = append(needIfaces, iface.Name)
		}
	}

	return hardware.WaitFabricReady(ctx, mod.log, hardware.WaitFabricReadyParams{
		StateProvider:  mod.devStateGetter,
		FabricIfaces:   needIfaces,
		IgnoreUnusable: true,
		IterationSleep: time.Second,
	})
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
