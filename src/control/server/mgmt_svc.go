//
// (C) Copyright 2018-2020 Intel Corporation.
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

package server

import (
	"fmt"
	"net"
	"os/exec"
	"strconv"
	"strings"
	"syscall"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"golang.org/x/net/context"
	"google.golang.org/grpc/peer"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/system"
)

const instanceUpdateDelay = 500 * time.Millisecond

// NewRankResult returns a reference to a new member result struct.
func NewRankResult(rank ioserver.Rank, action string, state system.MemberState, err error) *mgmtpb.RanksResp_RankResult {
	result := mgmtpb.RanksResp_RankResult{
		Rank: rank.Uint32(), Action: action, State: uint32(state),
	}
	if err != nil {
		result.Errored = true
		result.Msg = err.Error()
	}

	return &result
}

// drespToRankResult converts drpc.Response to proto.RanksResp_RankResult
//
// RankResult is populated with rank, state and error dependent on processing
// dRPC response. Target state param is populated on success, Errored otherwise.
func drespToRankResult(rank ioserver.Rank, action string, dresp *drpc.Response, err error, tState system.MemberState) *mgmtpb.RanksResp_RankResult {
	var outErr error
	state := system.MemberStateErrored

	if err != nil {
		outErr = errors.WithMessagef(err, "rank %s dRPC failed", &rank)
	} else {
		resp := &mgmtpb.DaosResp{}
		if err = proto.Unmarshal(dresp.Body, resp); err != nil {
			outErr = errors.WithMessagef(err, "rank %s dRPC unmarshal failed",
				&rank)
		} else if resp.GetStatus() != 0 {
			outErr = errors.Errorf("rank %s dRPC returned DER %d",
				&rank, resp.GetStatus())
		}
	}

	if outErr == nil {
		state = tState
	}

	return NewRankResult(rank, action, state, outErr)
}

// CheckReplica verifies if this server is supposed to host an MS replica,
// only performing the check and printing the result for now.
func CheckReplica(lis net.Listener, accessPoints []string, srv *exec.Cmd) (msReplicaCheck string, err error) {
	isReplica, bootstrap, err := checkMgmtSvcReplica(lis.Addr().(*net.TCPAddr), accessPoints)
	if err != nil {
		_ = srv.Process.Kill()
		return
	}

	if isReplica {
		msReplicaCheck = " as access point"
		if bootstrap {
			msReplicaCheck += " (bootstrap)"
		}
	}

	return
}

// getInterfaceAddrs enables TestCheckMgmtSvcReplica to replace the real
// interface query with a sample data set.
var getInterfaceAddrs = func() ([]net.Addr, error) {
	return net.InterfaceAddrs()
}

// checkMgmtSvcReplica determines if this server is supposed to host an MS
// replica, based on this server's management address and the system access
// points. If bootstrap is true, in which case isReplica must be true, this
// replica shall bootstrap the MS.
func checkMgmtSvcReplica(self *net.TCPAddr, accessPoints []string) (isReplica, bootstrap bool, err error) {
	replicas, err := resolveAccessPoints(accessPoints)
	if err != nil {
		return false, false, err
	}

	selves, err := getListenIPs(self)
	if err != nil {
		return false, false, err
	}

	// Check each replica against this server's listen IPs.
	for i := range replicas {
		if replicas[i].Port != self.Port {
			continue
		}
		for _, ip := range selves {
			if ip.Equal(replicas[i].IP) {
				// The first replica in the access point list
				// shall bootstrap the MS.
				if i == 0 {
					return true, true, nil
				}
				return true, false, nil
			}
		}
	}

	return false, false, nil
}

// resolveAccessPoints resolves the strings in accessPoints into addresses in
// addrs. If a port isn't specified, assume the default port.
func resolveAccessPoints(accessPoints []string) (addrs []*net.TCPAddr, err error) {
	defaultPort := NewConfiguration().ControlPort
	for _, ap := range accessPoints {
		if !common.HasPort(ap) {
			ap = net.JoinHostPort(ap, strconv.Itoa(defaultPort))
		}
		t, err := net.ResolveTCPAddr("tcp", ap)
		if err != nil {
			return nil, err
		}
		addrs = append(addrs, t)
	}
	return addrs, nil
}

// getListenIPs takes the address this server listens on and returns a list of
// the corresponding IPs.
func getListenIPs(listenAddr *net.TCPAddr) (listenIPs []net.IP, err error) {
	if listenAddr.IP.IsUnspecified() {
		// Find the IPs of all IP interfaces.
		addrs, err := getInterfaceAddrs()
		if err != nil {
			return nil, err
		}
		for _, a := range addrs {
			// Ignore non-IP interfaces.
			in, ok := a.(*net.IPNet)
			if ok {
				listenIPs = append(listenIPs, in.IP)
			}
		}
	} else {
		listenIPs = append(listenIPs, listenAddr.IP)
	}
	return listenIPs, nil
}

// mgmtSvc implements (the Go portion of) Management Service, satisfying
// mgmtpb.MgmtSvcServer.
type mgmtSvc struct {
	log        logging.Logger
	harness    *IOServerHarness
	membership *system.Membership // if MS leader, system membership list
}

func newMgmtSvc(h *IOServerHarness, m *system.Membership) *mgmtSvc {
	return &mgmtSvc{
		log:        h.log,
		harness:    h,
		membership: m,
	}
}

func (svc *mgmtSvc) GetAttachInfo(ctx context.Context, req *mgmtpb.GetAttachInfoReq) (*mgmtpb.GetAttachInfoResp, error) {
	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	dresp, err := mi.CallDrpc(drpc.ModuleMgmt, drpc.MethodGetAttachInfo, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.GetAttachInfoResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal GetAttachInfo response")
	}

	return resp, nil
}

// getPeerListenAddr combines peer ip from supplied context with input port.
func getPeerListenAddr(ctx context.Context, listenAddrStr string) (net.Addr, error) {
	p, ok := peer.FromContext(ctx)
	if !ok {
		return nil, errors.New("peer details not found in context")
	}

	tcpAddr, ok := p.Addr.(*net.TCPAddr)
	if !ok {
		return nil, errors.Errorf("peer address (%s) not tcp", p.Addr)
	}

	// what port is the input address listening on?
	_, portStr, err := net.SplitHostPort(listenAddrStr)
	if err != nil {
		return nil, errors.Wrap(err, "get listening port")
	}

	// resolve combined IP/port address
	return net.ResolveTCPAddr(p.Addr.Network(),
		net.JoinHostPort(tcpAddr.IP.String(), portStr))
}

func (svc *mgmtSvc) Join(ctx context.Context, req *mgmtpb.JoinReq) (*mgmtpb.JoinResp, error) {
	// combine peer (sender) IP (from context) with listening port (from
	// joining instance's host addr, in request params) as addr to reply to.
	replyAddr, err := getPeerListenAddr(ctx, req.GetAddr())
	if err != nil {
		return nil, errors.WithMessage(err,
			"combining peer addr with listener port")
	}

	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	dresp, err := mi.CallDrpc(drpc.ModuleMgmt, drpc.MethodJoin, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.JoinResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal Join response")
	}

	// if join successful, record membership
	if resp.GetStatus() == 0 {
		newState := system.MemberStateEvicted
		if resp.GetState() == mgmtpb.JoinResp_IN {
			newState = system.MemberStateStarted
		}

		member := system.NewMember(resp.GetRank(), req.GetUuid(), replyAddr, newState)

		created, oldState := svc.membership.AddOrUpdate(member)
		if created {
			svc.log.Debugf("new system member: rank %d, addr %s",
				resp.GetRank(), replyAddr)
		} else {
			svc.log.Debugf("updated system member: rank %d, addr %s, %s->%s",
				member.Rank, replyAddr, *oldState, newState)
			if *oldState == newState {
				svc.log.Errorf("unexpected same state in rank %d update (%s->%s)",
					member.Rank, *oldState, newState)
			}
		}
	}

	return resp, nil
}

// checkIsMSReplica provides a hint as to who is service leader if instance is
// not a Management Service replica.
func checkIsMSReplica(mi *IOServerInstance) error {
	msg := "instance is not an access point"
	if !mi.IsMSReplica() {
		leader, err := mi.msClient.LeaderAddress()
		if err != nil {
			return err
		}

		if !strings.HasPrefix(leader, "localhost") {
			msg += ", try " + leader
		}

		return errors.New(msg)
	}

	return nil
}

// PoolCreate implements the method defined for the Management Service.
//
// Validate minimum SCM/NVMe pool size per VOS target, pool size request params
// are per-ioserver so need to be larger than (minimum_target_allocation *
// target_count).
func (svc *mgmtSvc) PoolCreate(ctx context.Context, req *mgmtpb.PoolCreateReq) (*mgmtpb.PoolCreateResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}

	svc.log.Debugf("MgmtSvc.PoolCreate dispatch, req:%+v\n", *req)

	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	targetCount := mi.runner.GetConfig().TargetCount
	if targetCount == 0 {
		return nil, errors.New("zero target count")
	}
	if req.Scmbytes < ioserver.ScmMinBytesPerTarget*uint64(targetCount) {
		return nil, FaultPoolScmTooSmall(req.Scmbytes, targetCount)
	}
	if req.Nvmebytes != 0 && req.Nvmebytes < ioserver.NvmeMinBytesPerTarget*uint64(targetCount) {
		return nil, FaultPoolNvmeTooSmall(req.Nvmebytes, targetCount)
	}

	dresp, err := mi.CallDrpc(drpc.ModuleMgmt, drpc.MethodPoolCreate, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolCreateResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolCreate response")
	}

	svc.log.Debugf("MgmtSvc.PoolCreate dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// PoolDestroy implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolDestroy(ctx context.Context, req *mgmtpb.PoolDestroyReq) (*mgmtpb.PoolDestroyResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	if req.GetUuid() == "" {
		// TODO: do we want to validate pool exists via ListPools?
		return nil, errors.New("nil UUID")
	}

	svc.log.Debugf("MgmtSvc.PoolDestroy dispatch, req:%+v\n", *req)

	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	dresp, err := mi.CallDrpc(drpc.ModuleMgmt, drpc.MethodPoolDestroy, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolDestroyResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolDestroy response")
	}

	svc.log.Debugf("MgmtSvc.PoolDestroy dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// PoolReintegrate implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolReintegrate(ctx context.Context, req *mgmtpb.PoolReintegrateReq) (*mgmtpb.PoolReintegrateResp, error) {
	svc.log.Debugf("MgmtSvc.PoolReintegrate dispatch, req:%+v\n", *req)

	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	dresp, err := mi.CallDrpc(drpc.ModuleMgmt, drpc.MethodPoolReintegrate, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolReintegrateResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolReintegrate response")
	}

	svc.log.Debugf("MgmtSvc.PoolReintegrate dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// PoolQuery forwards a pool query request to the I/O server.
func (svc *mgmtSvc) PoolQuery(ctx context.Context, req *mgmtpb.PoolQueryReq) (*mgmtpb.PoolQueryResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}

	svc.log.Debugf("MgmtSvc.PoolQuery dispatch, req:%+v\n", *req)

	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	dresp, err := mi.CallDrpc(drpc.ModuleMgmt, drpc.MethodPoolQuery, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolQueryResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolQuery response")
	}

	svc.log.Debugf("MgmtSvc.PoolQuery dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// resolvePoolPropVal resolves string-based property names and values to their C equivalents.
func resolvePoolPropVal(req *mgmtpb.PoolSetPropReq) (*mgmtpb.PoolSetPropReq, error) {
	newReq := &mgmtpb.PoolSetPropReq{
		Uuid: req.Uuid,
	}

	propName := strings.TrimSpace(req.GetName())
	switch strings.ToLower(propName) {
	case "reclaim":
		newReq.SetPropertyNumber(drpc.PoolPropertySpaceReclaim)

		recType := strings.TrimSpace(req.GetStrval())
		switch strings.ToLower(recType) {
		case "disabled":
			newReq.SetValueNumber(drpc.PoolSpaceReclaimDisabled)
		case "lazy":
			newReq.SetValueNumber(drpc.PoolSpaceReclaimLazy)
		case "time":
			newReq.SetValueNumber(drpc.PoolSpaceReclaimTime)
		default:
			return nil, errors.Errorf("unhandled reclaim type %q", recType)
		}

		return newReq, nil
	default:
		return nil, errors.Errorf("unhandled pool property %q", propName)
	}
}

// PoolSetProp forwards a request to the I/O server to set a pool property.
func (svc *mgmtSvc) PoolSetProp(ctx context.Context, req *mgmtpb.PoolSetPropReq) (*mgmtpb.PoolSetPropResp, error) {
	svc.log.Debugf("MgmtSvc.PoolSetProp dispatch, req:%+v", *req)

	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	newReq, err := resolvePoolPropVal(req)
	if err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.PoolSetProp dispatch, req (converted):%+v", *newReq)

	dresp, err := mi.CallDrpc(drpc.ModuleMgmt, drpc.MethodPoolSetProp, newReq)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.PoolSetPropResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolSetProp response")
	}

	svc.log.Debugf("MgmtSvc.PoolSetProp dispatch, resp:%+v", *resp)

	if resp.GetNumber() != newReq.GetNumber() {
		return nil, errors.Errorf("Response number doesn't match request (%d != %d)",
			resp.GetNumber(), newReq.GetNumber())
	}
	// Restore the string versions of the property/value
	resp.Property = &mgmtpb.PoolSetPropResp_Name{
		Name: req.GetName(),
	}
	if req.GetStrval() != "" {
		if resp.GetNumval() != newReq.GetNumval() {
			return nil, errors.Errorf("Response value doesn't match request (%d != %d)",
				resp.GetNumval(), newReq.GetNumval())
		}
		resp.Value = &mgmtpb.PoolSetPropResp_Strval{
			Strval: req.GetStrval(),
		}
	}

	return resp, nil
}

// PoolGetACL forwards a request to the IO server to fetch a pool's Access Control List
func (svc *mgmtSvc) PoolGetACL(ctx context.Context, req *mgmtpb.GetACLReq) (*mgmtpb.ACLResp, error) {
	svc.log.Debugf("MgmtSvc.PoolGetACL dispatch, req:%+v\n", *req)

	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	dresp, err := mi.CallDrpc(drpc.ModuleMgmt, drpc.MethodPoolGetACL, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ACLResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolGetACL response")
	}

	svc.log.Debugf("MgmtSvc.PoolGetACL dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// PoolOverwriteACL forwards a request to the IO server to overwrite a pool's Access Control List
func (svc *mgmtSvc) PoolOverwriteACL(ctx context.Context, req *mgmtpb.ModifyACLReq) (*mgmtpb.ACLResp, error) {
	svc.log.Debugf("MgmtSvc.PoolOverwriteACL dispatch, req:%+v\n", *req)

	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	dresp, err := mi.CallDrpc(drpc.ModuleMgmt, drpc.MethodPoolOverwriteACL, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ACLResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolOverwriteACL response")
	}

	svc.log.Debugf("MgmtSvc.PoolOverwriteACL dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// PoolUpdateACL forwards a request to the IO server to add or update entries in
// a pool's Access Control List
func (svc *mgmtSvc) PoolUpdateACL(ctx context.Context, req *mgmtpb.ModifyACLReq) (*mgmtpb.ACLResp, error) {
	svc.log.Debugf("MgmtSvc.PoolUpdateACL dispatch, req:%+v\n", *req)

	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	dresp, err := mi.CallDrpc(drpc.ModuleMgmt, drpc.MethodPoolUpdateACL, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ACLResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolUpdateACL response")
	}

	svc.log.Debugf("MgmtSvc.PoolUpdateACL dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// PoolDeleteACL forwards a request to the IO server to delete an entry from a
// pool's Access Control List.
func (svc *mgmtSvc) PoolDeleteACL(ctx context.Context, req *mgmtpb.DeleteACLReq) (*mgmtpb.ACLResp, error) {
	svc.log.Debugf("MgmtSvc.PoolDeleteACL dispatch, req:%+v\n", *req)

	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	dresp, err := mi.CallDrpc(drpc.ModuleMgmt, drpc.MethodPoolDeleteACL, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ACLResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolDeleteACL response")
	}

	svc.log.Debugf("MgmtSvc.PoolDeleteACL dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// BioHealthQuery implements the method defined for the Management Service.
func (svc *mgmtSvc) BioHealthQuery(ctx context.Context, req *mgmtpb.BioHealthReq) (*mgmtpb.BioHealthResp, error) {
	svc.log.Debugf("MgmtSvc.BioHealthQuery dispatch, req:%+v\n", *req)

	// Iterate through the list of local I/O server instances, looking for
	// the first one that successfully fulfills the request. If none succeed,
	// return an error.
	for _, i := range svc.harness.Instances() {
		dresp, err := i.CallDrpc(drpc.ModuleMgmt, drpc.MethodBioHealth, req)
		if err != nil {
			return nil, err
		}

		resp := &mgmtpb.BioHealthResp{}
		if err = proto.Unmarshal(dresp.Body, resp); err != nil {
			return nil, errors.Wrap(err, "unmarshal BioHealthQuery response")
		}

		if resp.Status == 0 {
			return resp, nil
		}
	}

	reqID := func() string {
		if req.DevUuid != "" {
			return req.DevUuid
		}
		return req.TgtId
	}
	return nil, errors.Errorf("no rank matched request for %q", reqID())
}

// SmdListDevs implements the method defined for the Management Service.
func (svc *mgmtSvc) SmdListDevs(ctx context.Context, req *mgmtpb.SmdDevReq) (*mgmtpb.SmdDevResp, error) {
	svc.log.Debugf("MgmtSvc.SmdListDevs dispatch, req:%+v\n", *req)

	fullResp := new(mgmtpb.SmdDevResp)

	// Iterate through the list of local I/O server instances, and aggregate
	// results into a single response.
	for _, i := range svc.harness.Instances() {
		dresp, err := i.CallDrpc(drpc.ModuleMgmt, drpc.MethodSmdDevs, req)
		if err != nil {
			return nil, err
		}

		instResp := new(mgmtpb.SmdDevResp)
		if err = proto.Unmarshal(dresp.Body, instResp); err != nil {
			return nil, errors.Wrap(err, "unmarshal SmdListDevs response")
		}

		if instResp.Status != 0 {
			return instResp, nil
		}

		fullResp.Devices = append(fullResp.Devices, instResp.Devices...)
	}

	return fullResp, nil
}

// SmdListPools implements the method defined for the Management Service.
func (svc *mgmtSvc) SmdListPools(ctx context.Context, req *mgmtpb.SmdPoolReq) (*mgmtpb.SmdPoolResp, error) {
	svc.log.Debugf("MgmtSvc.SmdListPools dispatch, req:%+v\n", *req)

	fullResp := new(mgmtpb.SmdPoolResp)

	// Iterate through the list of local I/O server instances, and aggregate
	// results into a single response.
	for _, i := range svc.harness.Instances() {
		dresp, err := i.CallDrpc(drpc.ModuleMgmt, drpc.MethodSmdPools, req)
		if err != nil {
			return nil, err
		}

		instResp := new(mgmtpb.SmdPoolResp)
		if err = proto.Unmarshal(dresp.Body, instResp); err != nil {
			return nil, errors.Wrap(err, "unmarshal SmdListPools response")
		}

		if instResp.Status != 0 {
			return instResp, nil
		}

		fullResp.Pools = append(fullResp.Pools, instResp.Pools...)
	}

	return fullResp, nil
}

// DevStateQuery implements the method defined for the Management Service.
func (svc *mgmtSvc) DevStateQuery(ctx context.Context, req *mgmtpb.DevStateReq) (*mgmtpb.DevStateResp, error) {
	svc.log.Debugf("MgmtSvc.DevStateQuery dispatch, req:%+v\n", *req)

	// Iterate through the list of local I/O server instances, looking for
	// the first one that successfully fulfills the request. If none succeed,
	// return an error.
	for _, i := range svc.harness.Instances() {
		dresp, err := i.CallDrpc(drpc.ModuleMgmt, drpc.MethodDevStateQuery, req)
		if err != nil {
			return nil, err
		}

		resp := &mgmtpb.DevStateResp{}
		if err = proto.Unmarshal(dresp.Body, resp); err != nil {
			return nil, errors.Wrap(err, "unmarshal DevStateQuery response")
		}

		if resp.Status == 0 {
			return resp, nil
		}
	}

	return nil, errors.Errorf("no rank matched request for %q", req.DevUuid)
}

// StorageSetFaulty implements the method defined for the Management Service.
func (svc *mgmtSvc) StorageSetFaulty(ctx context.Context, req *mgmtpb.DevStateReq) (*mgmtpb.DevStateResp, error) {
	svc.log.Debugf("MgmtSvc.StorageSetFaulty dispatch, req:%+v\n", *req)

	// Iterate through the list of local I/O server instances, looking for
	// the first one that successfully fulfills the request. If none succeed,
	// return an error.
	for _, i := range svc.harness.Instances() {
		dresp, err := i.CallDrpc(drpc.ModuleMgmt, drpc.MethodSetFaultyState, req)
		if err != nil {
			return nil, err
		}

		resp := &mgmtpb.DevStateResp{}
		if err = proto.Unmarshal(dresp.Body, resp); err != nil {
			return nil, errors.Wrap(err, "unmarshal StorageSetFaulty response")
		}

		if resp.Status == 0 {
			return resp, nil
		}
	}

	return nil, errors.Errorf("no rank matched request for %q", req.DevUuid)
}

// checkRankList checks rank is present in rank list.
func checkRankList(rank ioserver.Rank, ranks []ioserver.Rank) bool {
	if len(ranks) == 0 {
		return true // empty rank list indicates no filtering
	}
	for _, r := range ranks {
		if rank.Equals(&r) {
			return true
		}
	}

	return false
}

// PrepShutdown implements the method defined for the Management Service.
//
// Prepare data-plane instance managed by control-plane for a controlled shutdown,
// identified by unique rank.
//
// Iterate over instances, issuing PrepShutdown dRPCs and record results.
// Return error in addition to response if any instance requests not successful
// so retries can be performed at sender.
func (svc *mgmtSvc) PrepShutdownRanks(ctx context.Context, req *mgmtpb.RanksReq) (*mgmtpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	svc.log.Debugf("MgmtSvc.PrepShutdown dispatch, req:%+v\n", *req)

	resp := &mgmtpb.RanksResp{}

	var rankList []ioserver.Rank
	if err := convert.Types(req.Ranks, &rankList); err != nil {
		return nil, errors.Wrap(err, "parsing request rank list")
	}

	for _, i := range svc.harness.Instances() {
		rank, err := i.GetRank()
		if err != nil {
			return nil, err
		}

		if !checkRankList(rank, rankList) {
			continue // filtered out, no result expected
		}

		if !i.IsStarted() {
			resp.Results = append(resp.Results,
				NewRankResult(rank, "prep shutdown",
					system.MemberStateStopped, nil))
			continue
		}

		dresp, err := i.CallDrpc(drpc.ModuleMgmt, drpc.MethodPrepShutdown, nil)

		resp.Results = append(resp.Results,
			drespToRankResult(rank, "prep shutdown", dresp, err,
				system.MemberStateStopping))
	}

	svc.log.Debugf("MgmtSvc.PrepShutdown dispatch, resp:%+v\n", *resp)

	return resp, nil
}

func (svc *mgmtSvc) getStartedResults(rankList []ioserver.Rank, desiredState system.MemberState, action string, stopErrs map[ioserver.Rank]error) (system.MemberResults, error) {
	results := make(system.MemberResults, 0, maxIOServers)
	for _, i := range svc.harness.Instances() {
		rank, err := i.GetRank()
		if err != nil {
			return nil, err
		}

		if !checkRankList(rank, rankList) {
			continue // filtered out, no result expected
		}

		state := system.MemberStateStarted
		if !i.IsStarted() {
			state = system.MemberStateStopped
		}

		var extraErrMsg string
		if len(stopErrs) > 0 {
			if stopErr, exists := stopErrs[rank]; exists {
				if stopErr == nil {
					return nil, errors.New("expected non-nil error in error map")
				}
				extraErrMsg = fmt.Sprintf(" (%s)", stopErr.Error())
			}
		}
		if state != desiredState {
			err = errors.Errorf("want %s, got %s%s", desiredState, state, extraErrMsg)
		}

		results = append(results, system.NewMemberResult(rank.Uint32(), action, err, state))
	}

	return results, nil
}

// StopRanks implements the method defined for the Management Service.
//
// Stop data-plane instance managed by control-plane identified by unique rank.
// After attempting to stop instances through harness (when either all instances
// are stopped or timeout has occurred, populate response results based on
// instance started state.
func (svc *mgmtSvc) StopRanks(parent context.Context, req *mgmtpb.RanksReq) (*mgmtpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	svc.log.Debugf("MgmtSvc.StopRanks dispatch, req:%+v\n", *req)

	resp := &mgmtpb.RanksResp{}

	var rankList []ioserver.Rank
	if err := convert.Types(req.Ranks, &rankList); err != nil {
		return nil, errors.Wrap(err, "parsing request rank list")
	}

	signal := syscall.SIGINT
	if req.Force {
		signal = syscall.SIGKILL
	}

	ctx, cancel := context.WithTimeout(parent, ioserverShutdownTimeout)
	defer cancel()

	stopErrs, err := svc.harness.StopInstances(ctx, signal, rankList...)
	if err != nil {
		if err != context.DeadlineExceeded {
			// unexpected error, fail without collecting rank results
			return nil, err
		}
		svc.log.Debug("deadline exceeded when stopping instances")
	}

	stopped := make(chan struct{})
	go func() {
		for {
			if len(svc.harness.StartedRanks()) != 0 {
				time.Sleep(instanceUpdateDelay)
				continue
			}
			close(stopped)
			break
		}
	}()

	select {
	case <-stopped:
	case <-time.After(svc.harness.rankReqTimeout):
		svc.log.Debug("deadline exceeded when waiting for instances to stop")
	}

	results, err := svc.getStartedResults(rankList, system.MemberStateStopped, "stop", stopErrs)
	if err != nil {
		return nil, err
	}

	if err := convert.Types(results, &resp.Results); err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.StopRanks dispatch, resp:%+v\n", *resp)

	return resp, nil
}

func ping(i *IOServerInstance, rank ioserver.Rank, timeout time.Duration) *mgmtpb.RanksResp_RankResult {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	resChan := make(chan *mgmtpb.RanksResp_RankResult)
	go func() {
		var err error
		var dresp *drpc.Response

		dresp, err = i.CallDrpc(drpc.ModuleMgmt, drpc.MethodPingRank, nil)

		select {
		case <-ctx.Done():
		case resChan <- drespToRankResult(rank, "ping", dresp, err, system.MemberStateStarted):
		}
	}()

	select {
	case result := <-resChan:
		return result
	case <-time.After(timeout):
		return NewRankResult(rank, "ping", system.MemberStateUnresponsive,
			errors.New("timeout occurred"))
	}
}

// PingRanks implements the method defined for the Management Service.
//
// Query data-plane all instances (DAOS system members) managed by harness to verify
// responsiveness.
//
// For each instance, call over dRPC and either return error for CallDrpc err or
// populate a RanksResp_RankResult in response. Result is either populated from
// return from dRPC which indicates activity and Status == 0, or in the case of a timeout
// the results status will be pingTimeoutStatus.
func (svc *mgmtSvc) PingRanks(ctx context.Context, req *mgmtpb.RanksReq) (*mgmtpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	svc.log.Debugf("MgmtSvc.PingRanks dispatch, req:%+v\n", *req)

	resp := &mgmtpb.RanksResp{}

	var rankList []ioserver.Rank
	if err := convert.Types(req.Ranks, &rankList); err != nil {
		return nil, errors.Wrap(err, "parsing request rank list")
	}

	for _, i := range svc.harness.Instances() {
		rank, err := i.GetRank()
		if err != nil {
			return nil, err
		}

		if !checkRankList(rank, rankList) {
			continue // filtered out, no result expected
		}

		if !i.IsStarted() {
			resp.Results = append(resp.Results,
				NewRankResult(rank, "ping", system.MemberStateStopped, nil))
			continue
		}

		resp.Results = append(resp.Results, ping(i, rank, svc.harness.rankReqTimeout))
	}

	svc.log.Debugf("MgmtSvc.PingRanks dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// StartRanks implements the method defined for the Management Service.
//
// Restart data-plane instances (DAOS system members) managed by harness.
//
// TODO: Current implementation sends restart signal to harness, restarting all
//       ranks managed by harness, future implementations will allow individual
//       ranks to be restarted. Work out how to start only a subsection of
//       instances based on ranks supplied in request.
func (svc *mgmtSvc) StartRanks(ctx context.Context, req *mgmtpb.RanksReq) (*mgmtpb.RanksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	svc.log.Debugf("MgmtSvc.StartRanks dispatch, req:%+v\n", *req)

	resp := &mgmtpb.RanksResp{}

	if err := svc.harness.RestartInstances(); err != nil {
		return nil, err
	}

	started := make(chan struct{})
	// select until instances start or timeout occurs (at which point get results of each instance)
	go func() {
		for {
			if len(svc.harness.StartedRanks()) != len(svc.harness.instances) {
				time.Sleep(instanceUpdateDelay)
				continue
			}
			close(started)
			break
		}
	}()

	select {
	case <-started:
	case <-time.After(svc.harness.rankReqTimeout):
	}

	var rankList []ioserver.Rank
	if err := convert.Types(req.Ranks, &rankList); err != nil {
		return nil, errors.Wrap(err, "parsing request rank list")
	}

	results, err := svc.getStartedResults(rankList, system.MemberStateStarted, "start", nil)
	if err != nil {
		return nil, err
	}
	if err := convert.Types(results, &resp.Results); err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.StartRanks dispatch, resp:%+v\n", *resp)

	return resp, nil
}

// ListPools forwards a gRPC request to the DAOS IO server to fetch a list of
// all pools in the system.
func (svc *mgmtSvc) ListPools(ctx context.Context, req *mgmtpb.ListPoolsReq) (*mgmtpb.ListPoolsResp, error) {
	svc.log.Debugf("MgmtSvc.ListPools dispatch, req:%+v\n", *req)

	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	dresp, err := mi.CallDrpc(drpc.ModuleMgmt, drpc.MethodListPools, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ListPoolsResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal ListPools response")
	}

	svc.log.Debugf("MgmtSvc.ListPools dispatch, resp:%+v\n", *resp)

	return resp, nil
}

func (svc *mgmtSvc) LeaderQuery(ctx context.Context, req *mgmtpb.LeaderQueryReq) (*mgmtpb.LeaderQueryResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}

	if len(svc.harness.instances) == 0 {
		return nil, errors.New("no I/O servers configured; can't determine leader")
	}

	instance := svc.harness.instances[0]
	sb := instance.getSuperblock()
	if sb == nil {
		return nil, errors.New("no I/O superblock found; can't determine leader")
	}

	if req.System != sb.System {
		return nil, errors.Errorf("received leader query for wrong system (local: %q, req: %q)",
			sb.System, req.System)
	}

	leaderAddr, err := instance.msClient.LeaderAddress()
	if err != nil {
		return nil, errors.Wrap(err, "failed to determine current leader address")
	}

	return &mgmtpb.LeaderQueryResp{
		CurrentLeader: leaderAddr,
		Replicas:      instance.msClient.cfg.AccessPoints,
	}, nil
}

// ListContainers implements the method defined for the Management Service.
func (svc *mgmtSvc) ListContainers(ctx context.Context, req *mgmtpb.ListContReq) (*mgmtpb.ListContResp, error) {
	svc.log.Debugf("MgmtSvc.ListContainers dispatch, req:%+v\n", *req)

	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	dresp, err := mi.CallDrpc(drpc.ModuleMgmt, drpc.MethodListContainers, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.ListContResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal ListContainers response")
	}

	svc.log.Debugf("MgmtSvc.ListContainers dispatch, resp:%+v\n", *resp)

	return resp, nil
}
