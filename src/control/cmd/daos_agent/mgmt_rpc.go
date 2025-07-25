//
// (C) Copyright 2019-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"net"
	"strings"

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
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/telemetry"
	"github.com/daos-stack/daos/src/control/lib/telemetry/promexp"
	"github.com/daos-stack/daos/src/control/logging"
)

// mgmtModule represents the daos_agent dRPC module. It acts mostly as a
// Management Service proxy, handling dRPCs sent by libdaos by forwarding them
// to MS.
type mgmtModule struct {
	log            logging.Logger
	sys            string
	ctlInvoker     control.Invoker
	cache          *InfoCache
	monitor        *procMon
	cliMetricsSrc  *promexp.ClientSource
	useDefaultNUMA atm.Bool

	numaGetter  hardware.ProcessNUMAProvider
	providerIdx uint
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
	case daos.MethodGetAttachInfo:
		return mod.handleGetAttachInfo(ctx, req, cred.Pid)
	case daos.MethodSetupClientTelemetry:
		return mod.handleSetupClientTelemetry(ctx, req, cred)
	case daos.MethodNotifyPoolConnect:
		return nil, mod.handleNotifyPoolConnect(ctx, req, cred.Pid)
	case daos.MethodNotifyPoolDisconnect:
		return nil, mod.handleNotifyPoolDisconnect(ctx, req, cred.Pid)
	case daos.MethodNotifyExit:
		// There isn't anything we can do here if this fails so just
		// call the disconnect handler and return success.
		mod.handleNotifyExit(ctx, cred.Pid)
		return nil, nil
	}

	return nil, drpc.UnknownMethodFailure()
}

// GetMethod returns the corresponding method for a given method ID.
func (mod *mgmtModule) GetMethod(id int32) (drpc.Method, error) {
	switch id {
	case daos.MethodGetAttachInfo.ID(),
		daos.MethodSetupClientTelemetry.ID(),
		daos.MethodNotifyPoolConnect.ID(),
		daos.MethodNotifyPoolDisconnect.ID(),
		daos.MethodNotifyExit.ID():
		return daos.MgmtMethod(id), nil
	}

	return nil, fmt.Errorf("invalid method ID %d for module %s", id, mod.String())
}

// ID returns the module ID for this module.
func (mod *mgmtModule) ID() int32 {
	return daos.ModuleMgmt
}

func (mod *mgmtModule) String() string {
	return "agent_mgmt"
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

	resp, err := mod.getAttachInfo(ctx, int(numaNode), pbReq)
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
	if mod.useDefaultNUMA.IsTrue() {
		return 0, nil
	}

	numaNode, err := mod.numaGetter.GetNUMANodeIDForPID(ctx, pid)
	if errors.Is(err, hardware.ErrNoNUMANodes) {
		mod.log.Debug("system is not NUMA-aware")
		mod.useDefaultNUMA.SetTrue()
		return 0, nil
	} else if err != nil {
		return 0, errors.Wrapf(err, "failed to get NUMA node ID for pid %d", pid)
	}

	return numaNode, nil
}

func (mod *mgmtModule) getAttachInfo(ctx context.Context, numaNode int, req *mgmtpb.GetAttachInfoReq) (*mgmtpb.GetAttachInfoResp, error) {
	rawResp, err := mod.getAttachInfoResp(ctx, req.Sys)
	if err != nil {
		mod.log.Errorf("failed to fetch AttachInfo: %s", err.Error())
		return nil, err
	}

	resp, err := mod.selectAttachInfo(ctx, rawResp, req.Interface, req.Domain)
	if err != nil {
		return nil, err
	}

	// Requested fabric interface/domain behave as a simple override. If we weren't able to
	// validate them, we return them to the user with the understanding that perhaps the user
	// knows what they're doing.
	// When an interface is requested without a domain, the domain is the same as the interface.
	iface := req.Interface
	domain := req.Domain
	if iface != "" && domain == "" {
		domain = iface
	}

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
	resp.ClientNetHint.Domain = domain
	mod.log.Tracef("D_DOMAIN for %s has been detected as: %s",
		resp.ClientNetHint.Interface, resp.ClientNetHint.Domain)

	if err := mod.populateNUMAFabricMap(ctx, resp); err != nil {
		return nil, err
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

func (mod *mgmtModule) selectAttachInfo(ctx context.Context, srvResp *mgmtpb.GetAttachInfoResp, iface, domain string) (*mgmtpb.GetAttachInfoResp, error) {
	resp := srvResp
	if mod.providerIdx > 0 {
		mod.log.Debugf("using secondary provider idx %d", mod.providerIdx)

		var err error
		// Secondary provider indices begin at 1
		resp, err = mod.selectSecondaryAttachInfo(srvResp, mod.providerIdx)
		if err != nil {
			return nil, err
		}
	}

	reqProviders := mod.getIfaceProviders(ctx, iface, domain, hardware.NetDevClass(resp.ClientNetHint.NetDevClass))
	if len(reqProviders) == 0 || reqProviders.Has(resp.ClientNetHint.Provider) {
		return resp, nil
	}

	return nil, fmt.Errorf("provider %s is not supported by requested interface %q domain %q (supports: %s)",
		resp.ClientNetHint.Provider, iface, domain, strings.Join(reqProviders.ToSlice(), ", "))
}

func (mod *mgmtModule) getIfaceProviders(ctx context.Context, iface, domain string, ndc hardware.NetDevClass) common.StringSet {
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
		DevClass:  ndc,
	}); err != nil {
		mod.log.Errorf("requested fabric interface %q (domain %q) may not function as desired: %s", iface, domain, err)
	} else if fis.NetDevClass != FabricDevClassManual {
		providers.Add(fis.Providers()...)
	}

	mod.log.Tracef("requested interface %q (domain: %q) supports providers: %q", iface, domain, strings.Join(providers.ToSlice(), ", "))
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

func (mod *mgmtModule) getFabricInterface(ctx context.Context, params *FabricIfaceParams) (*FabricInterface, error) {
	return mod.cache.GetFabricDevice(ctx, params)
}

func (mod *mgmtModule) populateNUMAFabricMap(ctx context.Context, resp *mgmtpb.GetAttachInfoResp) error {
	numaMap, unlockMap, err := mod.cache.GetNUMAFabricMap(ctx, hardware.NetDevClass(resp.ClientNetHint.NetDevClass), resp.ClientNetHint.Provider)
	if err != nil {
		return err
	}
	defer unlockMap()

	numNUMANodes := numaMap.MaxNUMANode() + 1 // NUMA indexed by zero
	resp.NumaFabricInterfaces = make([]*mgmtpb.FabricInterfaces, numNUMANodes)
	for numaNode := 0; numaNode < numNUMANodes; numaNode++ {
		pbFIs := &mgmtpb.FabricInterfaces{
			NumaNode: uint32(numaNode),
		}

		fis, exists := numaMap[numaNode]
		if exists {
			pbFIs.Ifaces = make([]*mgmtpb.FabricInterface, 0, len(fis))
			for _, fi := range fis {
				if fi.HasProvider(resp.ClientNetHint.Provider) || fi.NetDevClass == FabricDevClassManual {
					pbFIs.Ifaces = append(pbFIs.Ifaces, &mgmtpb.FabricInterface{
						NumaNode:  uint32(numaNode),
						Interface: fi.Name,
						Domain:    fi.Domain,
						Provider:  resp.ClientNetHint.Provider,
					})
				}
			}
		}

		resp.NumaFabricInterfaces[numaNode] = pbFIs
	}

	return nil
}

func (mod *mgmtModule) handleSetupClientTelemetry(ctx context.Context, reqb []byte, cred *unix.Ucred) ([]byte, error) {
	if len(reqb) == 0 {
		return nil, errors.New("empty request")
	}

	pbReq := new(mgmtpb.ClientTelemetryReq)
	if err := proto.Unmarshal(reqb, pbReq); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}
	if pbReq.Jobid == "" {
		return nil, errors.New("empty jobid")
	}
	if pbReq.ShmKey == 0 {
		return nil, errors.New("unset shm key")
	}
	if cred == nil {
		return nil, errors.New("nil user credentials")
	}

	resp := &mgmtpb.ClientTelemetryResp{AgentUid: int32(unix.Getuid())}
	if err := telemetry.SetupClientRoot(ctx, pbReq.Jobid, int(cred.Pid), int(pbReq.ShmKey)); err != nil {
		if cause, ok := errors.Cause(err).(daos.Status); ok {
			resp.Status = int32(cause)
		} else {
			return nil, err
		}
	}
	mod.log.Tracef("%d: %s", cred.Pid, pblog.Debug(resp))
	return proto.Marshal(resp)
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
