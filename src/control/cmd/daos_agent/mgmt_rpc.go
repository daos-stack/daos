//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"net"
	"strings"
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

	log            logging.Logger
	sys            string
	ctlInvoker     control.Invoker
	attachInfo     *attachInfoCache
	fabricInfo     *localFabricCache
	monitor        *procMon
	useDefaultNUMA bool

	numaGetter     hardware.ProcessNUMAProvider
	providerIdx    uint
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
	if fault.IsFaultCode(err, code.ServerWrongSystem) {
		resp = &mgmtpb.GetAttachInfoResp{Status: int32(daos.ControlIncompatible)}
	} else if err != nil {
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

	resp, err := mod.selectAttachInfo(ctx, rawResp, req.Interface, req.Domain)
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

func (mod *mgmtModule) selectAttachInfo(ctx context.Context, srvResp *mgmtpb.GetAttachInfoResp, iface, domain string) (*mgmtpb.GetAttachInfoResp, error) {
	reqProviders := mod.getIfaceProviders(ctx, iface, domain)
	mod.log.Debugf("requested interface %q (domain: %q) supports providers: %s", iface, domain, strings.Join(reqProviders.ToSlice(), ", "))

	if mod.providerIdx > 0 {
		// Secondary provider indices begin at 1
		resp, err := mod.selectSecondaryAttachInfo(srvResp, mod.providerIdx)
		if err != nil {
			return nil, err
		}

		if len(reqProviders) != 0 && !reqProviders.Has(resp.ClientNetHint.Provider) {
			mod.log.Errorf("requested fabric interface %q (domain: %q) does not report support for configured provider %q (idx %d)",
				iface, domain, resp.ClientNetHint.Provider, mod.providerIdx)
		}

		return resp, nil
	}

	if len(reqProviders) == 0 || reqProviders.Has(srvResp.ClientNetHint.Provider) {
		return srvResp, nil
	}

	mod.log.Debugf("primary provider is not supported by requested interface %q domain %q (supports: %s)", iface, domain, strings.Join(reqProviders.ToSlice(), ", "))

	// We can try to be smart about choosing a provider if the client requested a specific interface
	for _, hint := range srvResp.SecondaryClientNetHints {
		if reqProviders.Has(hint.Provider) {
			mod.log.Debugf("found secondary provider supported by requested interface: %q (idx %d)", hint.Provider, hint.ProviderIdx)
			return mod.selectSecondaryAttachInfo(srvResp, uint(hint.ProviderIdx))
		}
	}

	mod.log.Errorf("no supported provider for requested interface %q domain %q, using primary by default")
	return srvResp, nil
}

func (mod *mgmtModule) getIfaceProviders(ctx context.Context, iface, domain string) common.StringSet {
	providers := common.NewStringSet()
	if iface == "" {
		return providers
	}

	if domain == "" {
		domain = iface
	}

	if fis, err := mod.getFabricInterface(ctx, &FabricIfaceParams{
		Interface: iface,
		Domain:    domain,
	}); err != nil {
		mod.log.Errorf("requested fabric interface %q (domain %q) may not function as desired: %s", iface, domain, err)
	} else {
		providers.Add(fis.Providers()...)
	}

	return providers
}

func (mod *mgmtModule) selectSecondaryAttachInfo(srvResp *mgmtpb.GetAttachInfoResp, provIdx uint) (*mgmtpb.GetAttachInfoResp, error) {
	if provIdx == 0 {
		return nil, errors.New("provider index 0 is not a secondary provider")
	}
	maxIdx := len(srvResp.SecondaryClientNetHints)
	if int(provIdx) > maxIdx {
		return nil, errors.Errorf("provider index %d out of range (maximum: %d)", provIdx, maxIdx)
	}

	hint := srvResp.SecondaryClientNetHints[provIdx-1]
	if hint.ProviderIdx != uint32(provIdx) {
		return nil, errors.Errorf("malformed network hint: expected provider index %d, got %d", provIdx, hint.ProviderIdx)
	}
	mod.log.Debugf("getting secondary provider %s URIs", hint.Provider)
	uris, err := mod.getProviderIdxURIs(srvResp, provIdx)
	if err != nil {
		return nil, err
	}

	return &mgmtpb.GetAttachInfoResp{
		Status:        srvResp.Status,
		RankUris:      uris,
		MsRanks:       srvResp.MsRanks,
		ClientNetHint: hint,
	}, nil
}

func (mod *mgmtModule) getProviderIdxURIs(srvResp *mgmtpb.GetAttachInfoResp, idx uint) ([]*mgmtpb.GetAttachInfoResp_RankUri, error) {
	uris := []*mgmtpb.GetAttachInfoResp_RankUri{}
	for _, uri := range srvResp.SecondaryRankUris {
		if uri.ProviderIdx == uint32(idx) {
			uris = append(uris, uri)
		}
	}

	if len(uris) == 0 {
		return nil, errors.Errorf("no rank URIs for provider idx %d", mod.providerIdx)
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
