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
	"syscall"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"golang.org/x/net/context"
	"google.golang.org/grpc"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	//	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
)

// mgmtModule represents the daos_agent dRPC module. It acts mostly as a
// Management Service proxy, handling dRPCs sent by libdaos by forwarding them
// to MS.
type mgmtModule struct {
	log logging.Logger
	sys string
	// The access point
	ap        string
	tcfg      *security.TransportConfig
	aiCache   *attachInfoCache
	numaAware bool
}

func (mod *mgmtModule) HandleCall(session *drpc.Session, method int32, req []byte) ([]byte, error) {
	switch method {
	case drpc.MethodGetAttachInfo:

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
		cred, err := syscall.GetsockoptUcred(fd, syscall.SOL_SOCKET, syscall.SO_PEERCRED)
		if err != nil {
			return nil, err
		}

		return mod.handleGetAttachInfo(req, cred.Pid)
	default:
		return nil, drpc.UnknownMethodFailure()
	}
}

func (mod *mgmtModule) ID() int32 {
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
func (mod *mgmtModule) handleGetAttachInfo(reqb []byte, pid int32) ([]byte, error) {
	var err error
	/*
		numaNode := defaultNumaNode

		if mod.numaAware {
			numaNode, err = netdetect.GetNUMASocketIDForPid(pid)
			if err != nil {
				return nil, err
			}
		}

		if mod.aiCache.isCached() {
			return mod.aiCache.getResponse(numaNode)
		}
	*/
	req := &mgmtpb.GetAttachInfoReq{}
	if err := proto.Unmarshal(reqb, req); err != nil {
		return nil, drpc.UnmarshalingPayloadFailure()
	}

	mod.log.Debugf("GetAttachInfo %s %v", mod.ap, *req)

	if req.Sys != mod.sys {
		return nil, errors.Errorf("unknown system name %s", req.Sys)
	}

	dialOpt, err := security.DialOptionForTransportConfig(mod.tcfg)
	if err != nil {
		return nil, err
	}
	opts := []grpc.DialOption{dialOpt}

	conn, err := grpc.Dial(mod.ap, opts...)
	if err != nil {
		return nil, errors.Wrapf(err, "dial %s", mod.ap)
	}
	defer conn.Close()

	client := mgmtpb.NewMgmtSvcClient(conn)

	resp, err := client.GetAttachInfo(context.Background(), req)
	if err != nil {
		return nil, errors.Wrapf(err, "GetAttachInfo %s %v", mod.ap, *req)
	}

	if resp.Provider == "" {
		return nil, errors.Errorf("GetAttachInfo %s %v contained no provider.", mod.ap, *req)
	}

	marshResp, err := proto.Marshal(resp)
	if err != nil {
		return nil, drpc.MarshalingFailure()
	}
	return marshResp, err

	/*
		// Scan the local fabric to determine what devices are available that match our provider
		scanResults, err := netdetect.ScanFabric(resp.Provider)
		if err != nil {
			return nil, err
		}

		err = mod.aiCache.initResponseCache(resp, scanResults)
		if err != nil {
			return nil, err
		}

		return mod.aiCache.getResponse(numaNode)
	*/
}
