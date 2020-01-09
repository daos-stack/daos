//
// (C) Copyright 2018-2019 Intel Corporation.
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
	"net"
	"os/exec"
	"strconv"
	"strings"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"golang.org/x/net/context"
	"google.golang.org/grpc/peer"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	pingTimeout       = 3 * time.Second
	pingTimeoutStatus = -1
	startTimeout      = 3 * time.Second
	notStartedStatus  = -1
	stopTimeout       = 3 * time.Second
	notStoppedStatus  = -1
)

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
		if !hasPort(ap) {
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

// hasPort checks if addr specifies a port. This only works with IPv4
// addresses at the moment.
func hasPort(addr string) bool {
	return strings.Contains(addr, ":")
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
func (svc *mgmtSvc) PoolCreate(ctx context.Context, req *mgmtpb.PoolCreateReq) (*mgmtpb.PoolCreateResp, error) {
	svc.log.Debugf("MgmtSvc.PoolCreate dispatch, req:%+v\n", *req)

	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
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

	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	dresp, err := mi.CallDrpc(drpc.ModuleMgmt, drpc.MethodBioHealth, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.BioHealthResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal BioHealthQuery response")
	}

	return resp, nil
}

// SmdListDevs implements the method defined for the Management Service.
func (svc *mgmtSvc) SmdListDevs(ctx context.Context, req *mgmtpb.SmdDevReq) (*mgmtpb.SmdDevResp, error) {
	svc.log.Debugf("MgmtSvc.SmdListDevs dispatch, req:%+v\n", *req)

	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	dresp, err := mi.CallDrpc(drpc.ModuleMgmt, drpc.MethodSmdDevs, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.SmdDevResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal SmdListDevs response")
	}

	return resp, nil
}

// SmdListPools implements the method defined for the Management Service.
func (svc *mgmtSvc) SmdListPools(ctx context.Context, req *mgmtpb.SmdPoolReq) (*mgmtpb.SmdPoolResp, error) {
	svc.log.Debugf("MgmtSvc.SmdListPools dispatch, req:%+v\n", *req)

	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	dresp, err := mi.CallDrpc(drpc.ModuleMgmt, drpc.MethodSmdPools, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.SmdPoolResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal SmdListPools response")
	}

	return resp, nil
}

// DevStateQuery implements the method defined for the Management Service.
func (svc *mgmtSvc) DevStateQuery(ctx context.Context, req *mgmtpb.DevStateReq) (*mgmtpb.DevStateResp, error) {
	svc.log.Debugf("MgmtSvc.DevStateQuery dispatch, req:%+v\n", *req)

	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	dresp, err := mi.CallDrpc(drpc.ModuleMgmt, drpc.MethodDevStateQuery, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.DevStateResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal DevStateQuery response")
	}

	return resp, nil
}

// StorageSetFaulty implements the method defined for the Management Service.
func (svc *mgmtSvc) StorageSetFaulty(ctx context.Context, req *mgmtpb.DevStateReq) (*mgmtpb.DevStateResp, error) {
	svc.log.Debugf("MgmtSvc.StorageSetFaulty dispatch, req:%+v\n", *req)

	mi, err := svc.harness.GetMSLeaderInstance()
	if err != nil {
		return nil, err
	}

	dresp, err := mi.CallDrpc(drpc.ModuleMgmt, drpc.MethodSetFaultyState, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.DevStateResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal StorageSetFaulty response")
	}

	return resp, nil
}

// validateInstanceRank checks instance rank in superblock matches supplied list.
func validateInstanceRank(i *IOServerInstance, ranks []uint32) (*uint32, bool) {
	if len(ranks) == 0 || !i.hasSuperblock() {
		return nil, false
	}
	rank := i.getSuperblock().Rank.Uint32()

	for _, r := range ranks {
		if r == rank {
			return &rank, true
		}
	}

	return nil, false
}

// PrepShutdown implements the method defined for the Management Service.
//
// Prepare data-plane instance managed by control-plane for a controlled shutdown,
// identified by unique rank.
//
// Iterate over instances, issuing PrepShutdown dRPCs and record results.
// Return error in addition to response if any instance requests not successful
// so retries can be performed at sender.
func (svc *mgmtSvc) PrepShutdown(ctx context.Context, req *mgmtpb.RanksReq) (*mgmtpb.RanksResp, error) {
	svc.log.Debugf("MgmtSvc.PrepShutdown dispatch, req:%+v\n", *req)

	var errored bool
	resp := &mgmtpb.RanksResp{}

	for _, i := range svc.harness.Instances() {
		rank, ok := validateInstanceRank(i, req.Ranks)
		if !ok {
			continue
		}

		var status int32
		dresp, err := i.CallDrpc(drpc.ModuleMgmt, drpc.MethodPrepShutdown, nil)
		if err != nil {
			// just log failure to allow retries and other instances to be processed
			svc.log.Errorf("rank %d drpc failed: %s", *rank, err)
			status = -1
		}

		if status == 0 {
			status = int32(dresp.Status)
		}

		if status != 0 {
			errored = true
		}

		resp.Results = append(resp.Results,
			&mgmtpb.RanksResp_RankResult{Rank: *rank, Status: status})
	}

	svc.log.Debugf("MgmtSvc.PrepShutdown dispatch, resp:%+v\n", *resp)

	if errored {
		return resp, errors.New("not all ranks have been prepared for shutdown")
	}

	return resp, nil
}

// KillRanks implements the method defined for the Management Service.
//
// Stop data-plane instance managed by control-plane identified by unique rank.
//
// Iterate over instances issuing kill rank requests, wait until either all instances are
// stopped or timeout occurs and then populate response results based on instance started state.
// Return error if any instances are still running in order to enable retries at the sender.
//
// TODO: Enable "force" if number of retries fail, issuing a different signal/mechanism for
//       terminating the process.
func (svc *mgmtSvc) KillRanks(ctx context.Context, req *mgmtpb.RanksReq) (*mgmtpb.RanksResp, error) {
	svc.log.Debugf("MgmtSvc.KillRanks dispatch, req:%+v\n", *req)

	resp := &mgmtpb.RanksResp{}

	for _, i := range svc.harness.Instances() {
		rank, ok := validateInstanceRank(i, req.Ranks)
		if !ok {
			continue
		}

		dresp, err := i.CallDrpc(drpc.ModuleMgmt, drpc.MethodKillRank, nil)
		if err != nil {
			// just log failure to allow retries and other instances to be processed
			svc.log.Errorf("rank %d drpc failed: %s", *rank, err)
		}
		if dresp.Status != 0 {
			svc.log.Errorf("rank %d KillRank drpc status returned %d", *rank, int32(dresp.Status))
		}
	}

	stopChan := make(chan struct{})
	// select until instances stop or timeout occurs (at which point get results of each instance)
	go func() {
		for {
			allStopped := true

			for _, instance := range svc.harness.Instances() {
				if instance.IsStarted() {
					allStopped = false
					continue
				}
			}

			if allStopped {
				break
			}

			time.Sleep(500 * time.Microsecond)
		}

		close(stopChan)
	}()

	select {
	case <-stopChan:
	case <-time.After(stopTimeout):
	}

	var errored bool
	// either all instances started or Timeout
	for _, i := range svc.harness.Instances() {
		if !i.hasSuperblock() {
			continue // FIXME: do we need to report this?
		}

		result := &mgmtpb.RanksResp_RankResult{Rank: i.getSuperblock().Rank.Uint32()}
		if i.IsStarted() {
			errored = true
			result.Status = notStoppedStatus
		}
		resp.Results = append(resp.Results, result)
	}

	svc.log.Debugf("MgmtSvc.KillRanks dispatch, resp:%+v\n", *resp)

	if errored {
		return resp, errors.New("not all ranks have been killed")
	}

	return resp, nil
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
	svc.log.Debugf("MgmtSvc.PingRanks dispatch, req:%+v\n", *req)

	resp := &mgmtpb.RanksResp{}

	for _, i := range svc.harness.Instances() {
		rank, ok := validateInstanceRank(i, req.Ranks)
		if !ok {
			continue
		}

		resChan := make(chan *mgmtpb.RanksResp_RankResult, 1)
		errChan := make(chan error, 1)
		go func() {
			dresp, err := i.CallDrpc(drpc.ModuleMgmt, drpc.MethodPingRank, nil)
			if err != nil {
				errChan <- err
			}
			if dresp.Status != 0 {
				errChan <- errors.Errorf("unexpected return status from PingRank dRPC (%d)",
					int(dresp.Status))
			}

			resChan <- &mgmtpb.RanksResp_RankResult{Rank: *rank, Status: 0}
		}()

		select {
		case err := <-errChan:
			return nil, errors.Wrapf(err, "rank %d drpc failed", *rank)
		case result := <-resChan:
			resp.Results = append(resp.Results, result)
		case <-time.After(pingTimeout):
			resp.Results = append(resp.Results,
				&mgmtpb.RanksResp_RankResult{Rank: *rank, Status: pingTimeoutStatus})
		}
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
	svc.log.Debugf("MgmtSvc.StartRanks dispatch, req:%+v\n", *req)

	resp := &mgmtpb.RanksResp{}

	// perform controlled restart of I/O Server harness
	if err := svc.harness.RestartInstances(); err != nil {
		return nil, err
	}

	startChan := make(chan struct{})
	// select until instances start or timeout occurs (at which point get results of each instance)
	go func() {
		for {
			allStarted := true

			for _, instance := range svc.harness.Instances() {
				if !instance.IsStarted() {
					allStarted = false
					continue
				}
			}

			if allStarted {
				break
			}

			time.Sleep(500 * time.Microsecond)
		}

		close(startChan)
	}()

	select {
	case <-startChan:
	case <-time.After(startTimeout):
	}

	// either all instances started or Timeout
	for _, i := range svc.harness.Instances() {
		if !i.hasSuperblock() {
			continue // FIXME: do we need to report this?
		}

		result := &mgmtpb.RanksResp_RankResult{Rank: i.getSuperblock().Rank.Uint32()}
		if !i.IsStarted() {
			result.Status = notStartedStatus
		}
		resp.Results = append(resp.Results, result)
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

	if len(svc.harness.Instances()) == 0 {
		return nil, errors.New("no I/O servers configured; can't determine leader")
	}

	instance := svc.harness.Instances()[0]
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
