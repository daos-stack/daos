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

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"golang.org/x/net/context"
	"google.golang.org/grpc/peer"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
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
	membership *common.Membership // if MS leader, system membership list
}

func newMgmtSvc(h *IOServerHarness, m *common.Membership) *mgmtSvc {
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
	if resp.GetStatus() == 0 && resp.GetState() == mgmtpb.JoinResp_IN {
		newMember := common.SystemMember{
			Addr: replyAddr, Uuid: req.GetUuid(), Rank: resp.GetRank(),
		}

		count, err := svc.membership.Add(newMember)
		if err != nil {
			return nil, errors.WithMessage(err, "adding to membership")
		}

		svc.log.Debugf("new system member: %s (total %d)\n", newMember, count)
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

// KillRank implements the method defined for the Management Service.
//
// Stop data-plane instance managed by control-plane identified by unique rank.
func (svc *mgmtSvc) KillRank(ctx context.Context, req *mgmtpb.KillRankReq) (*mgmtpb.DaosResp, error) {
	svc.log.Debugf("MgmtSvc.KillRank dispatch, req:%+v\n", *req)

	var mi *IOServerInstance
	for _, i := range svc.harness.Instances() {
		if i.hasSuperblock() && i.getSuperblock().Rank.Equals(ioserver.NewRankPtr(req.Rank)) {
			mi = i
			break
		}
	}

	if mi == nil {
		return nil, errors.Errorf("rank %d not found on this server", req.Rank)
	}

	dresp, err := mi.CallDrpc(drpc.ModuleMgmt, drpc.MethodKillRank, req)
	if err != nil {
		return nil, err
	}

	resp := &mgmtpb.DaosResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal DAOS response")
	}

	svc.log.Debugf("MgmtSvc.KillRank dispatch, resp:%+v\n", *resp)

	return resp, nil
}
