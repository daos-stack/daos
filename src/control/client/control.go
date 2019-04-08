//
// (C) Copyright 2018 Intel Corporation.
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

package client

import (
	"fmt"

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"

	"google.golang.org/grpc"
	"google.golang.org/grpc/connectivity"
)

var errConnect = fmt.Errorf("no client connection was found, please connect")

// ScmModules is an alias for protobuf ScmModule message slice representing
// a number of SCM modules installed on a storage node.
type ScmModules []*pb.ScmModule

// NvmeControllers is an alias for protobuf NvmeController message slice
// representing a number of NVMe SSD controllers installed on a storage node.
type NvmeControllers []*pb.NvmeController

// Control interface accesses gRPC client functionality.
type Control interface {
	connect(string) error
	disconnect() error
	connected() (connectivity.State, bool)
	getAddress() string
	listAllFeatures() (FeatureMap, error)
	listScmModules() (ScmModules, error)
	listNvmeCtrlrs() (NvmeControllers, error)
	formatStorage() error
	killRank(uuid string, rank uint32) error
}

// control is an abstraction around the MgmtControlClient
// generated by gRPC. It provides a simplified mechanism so users can
// minimize their use of protobuf datatypes.
type control struct {
	client pb.MgmtControlClient
	gconn  *grpc.ClientConn
}

// connect provides an easy interface to connect to Mgmt DAOS server.
//
// It takes address and port in a string.
//	addr: address and port number separated by a ":"
func (c *control) connect(addr string) (err error) {
	var opts []grpc.DialOption
	opts = append(opts, grpc.WithInsecure())

	conn, err := grpc.Dial(addr, opts...)
	if err != nil {
		return
	}
	c.client = pb.NewMgmtControlClient(conn)
	c.gconn = conn

	return
}

// disconnect terminates the underlying channel used by the grpc
// client service.
func (c *control) disconnect() error { return c.gconn.Close() }

// getAddress returns the target address of the connection.
func (c *control) getAddress() string { return c.gconn.Target() }

func checkState(state connectivity.State) bool {
	return (state == connectivity.Idle || state == connectivity.Ready)
}

// connected determines if the underlying socket connection is alive and well.
func (c *control) connected() (state connectivity.State, ok bool) {
	if c.gconn == nil {
		return
	}

	state = c.gconn.GetState()
	return state, checkState(state)
}
