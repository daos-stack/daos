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
	"sync"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
)

// CheckReplica verifies if this server is supposed to host an MS replica,
// only performing the check and printing the result for now.
//
// TODO: format and start the MS replica
func CheckReplica(
	lis net.Listener, accessPoints []string, srv *exec.Cmd) (
	msReplicaCheck string, err error) {

	isReplica, bootstrap, err := checkMgmtSvcReplica(
		lis.Addr().(*net.TCPAddr), accessPoints)
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
// pb.MgmtSvcServer.
type mgmtSvc struct {
	log     logging.Logger
	mutex   sync.Mutex
	harness *IOServerHarness
}

func newMgmtSvc(h *IOServerHarness) *mgmtSvc {
	return &mgmtSvc{
		log:     h.log,
		harness: h,
	}
}

func (svc *mgmtSvc) GetAttachInfo(ctx context.Context, req *pb.GetAttachInfoReq) (*pb.GetAttachInfoResp, error) {
	mi, err := svc.harness.GetManagementInstance()
	if err != nil {
		return nil, err
	}

	svc.mutex.Lock()
	dresp, err := makeDrpcCall(mi.drpcClient, mgmtModuleID, getAttachInfo, req)
	svc.mutex.Unlock()
	if err != nil {
		return nil, err
	}

	resp := &pb.GetAttachInfoResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal GetAttachInfo response")
	}

	return resp, nil
}

func (svc *mgmtSvc) Join(ctx context.Context, req *pb.JoinReq) (*pb.JoinResp, error) {
	mi, err := svc.harness.GetManagementInstance()
	if err != nil {
		return nil, err
	}

	svc.mutex.Lock()
	dresp, err := makeDrpcCall(mi.drpcClient, mgmtModuleID, join, req)
	svc.mutex.Unlock()
	if err != nil {
		return nil, err
	}

	resp := &pb.JoinResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal Join response")
	}

	return resp, nil
}

// PoolCreate implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolCreate(ctx context.Context, req *pb.PoolCreateReq) (*pb.PoolCreateResp, error) {
	mi, err := svc.harness.GetManagementInstance()
	if err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.PoolCreate dispatch, req:%+v\n", *req)

	svc.mutex.Lock()
	dresp, err := makeDrpcCall(mi.drpcClient, mgmtModuleID, poolCreate, req)
	svc.mutex.Unlock()
	if err != nil {
		return nil, err
	}

	resp := &pb.PoolCreateResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolCreate response")
	}

	return resp, nil
}

// PoolDestroy implements the method defined for the Management Service.
func (svc *mgmtSvc) PoolDestroy(ctx context.Context, req *pb.PoolDestroyReq) (*pb.PoolDestroyResp, error) {
	mi, err := svc.harness.GetManagementInstance()
	if err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.PoolDestroy dispatch, req:%+v\n", *req)

	svc.mutex.Lock()
	dresp, err := makeDrpcCall(mi.drpcClient, mgmtModuleID, poolDestroy, req)
	svc.mutex.Unlock()
	if err != nil {
		return nil, err
	}

	resp := &pb.PoolDestroyResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal PoolDestroy response")
	}

	return resp, nil
}

// KillRank implements the method defined for the Management Service.
func (svc *mgmtSvc) KillRank(ctx context.Context, req *pb.DaosRank) (*pb.DaosResp, error) {
	mi, err := svc.harness.GetManagementInstance()
	if err != nil {
		return nil, err
	}

	svc.log.Debugf("MgmtSvc.KillRank dispatch, req:%+v\n", *req)

	svc.mutex.Lock()
	dresp, err := makeDrpcCall(mi.drpcClient, mgmtModuleID, killRank, req)
	svc.mutex.Unlock()
	if err != nil {
		return nil, err
	}

	resp := &pb.DaosResp{}
	if err = proto.Unmarshal(dresp.Body, resp); err != nil {
		return nil, errors.Wrap(err, "unmarshal DAOS response")
	}

	return resp, nil
}
