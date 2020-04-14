//
// (C) Copyright 2020 Intel Corporation.
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

package control

import (
	"context"
	"os/user"
	"strings"

	"github.com/golang/protobuf/proto"
	"github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

const (
	defaultSvcReps = 1
)

// checkUUID is a helper function for validating that the supplied
// UUID string parses as a valid UUID.
func checkUUID(uuidStr string) error {
	_, err := uuid.Parse(uuidStr)
	return errors.Wrapf(err, "invalid UUID %q", uuidStr)
}

// formatNameGroup converts system names to principal and if both user and group
// are unspecified, takes effective user name and that user's primary group.
func formatNameGroup(usr string, grp string) (string, string, error) {
	if usr == "" && grp == "" {
		eUsr, err := user.Current()
		if err != nil {
			return "", "", err
		}

		eGrp, err := user.LookupGroupId(eUsr.Gid)
		if err != nil {
			return "", "", err
		}

		usr, grp = eUsr.Username, eGrp.Name
	}

	if usr != "" && !strings.Contains(usr, "@") {
		usr += "@"
	}

	if grp != "" && !strings.Contains(grp, "@") {
		grp += "@"
	}

	return usr, grp, nil
}

// genPoolCreateRequest takes a *PoolCreateRequest and generates a valid protobuf
// request, filling in any missing fields with reasonable defaults.
func genPoolCreateRequest(in *PoolCreateReq) (out *mgmtpb.PoolCreateReq, err error) {
	// ensure pool ownership is set up correctly
	in.User, in.UserGroup, err = formatNameGroup(in.User, in.UserGroup)
	if err != nil {
		return nil, err
	}

	if in.NumSvcReps == 0 {
		in.NumSvcReps = defaultSvcReps
	}

	// ensure we have a system name in the request
	if in.Sys == "" {
		in.Sys = build.DefaultSystemName
	}

	out = new(mgmtpb.PoolCreateReq)
	if err = convert.Types(in, out); err != nil {
		return nil, err
	}

	// generate a UUID if not supplied in the request
	if err := checkUUID(out.Uuid); err != nil {
		if out.Uuid != "" {
			return nil, err
		}

		genUUID, err := uuid.NewRandom()
		if err != nil {
			return nil, err
		}
		out.Uuid = genUUID.String()
	}

	return
}

type (
	// PoolCreateReq contains the parameters for a pool create request.
	PoolCreateReq struct {
		msRequest
		unaryRequest
		ScmBytes   uint64
		NvmeBytes  uint64
		Ranks      []uint32
		NumSvcReps uint32
		Sys        string
		User       string
		UserGroup  string
		ACL        *AccessControlList
		UUID       string
	}

	// PoolCreateResp contains the response from a pool create request.
	PoolCreateResp struct {
		UUID    string
		SvcReps []uint32
	}
)

// PoolCreate performs a pool create operation on a DAOS Management Server instance.
// Default values for missing request parameters (e.g. owner/group) are generated when
// appropriate.
func PoolCreate(ctx context.Context, rpcClient UnaryInvoker, req *PoolCreateReq) (*PoolCreateResp, error) {
	pbReq, err := genPoolCreateRequest(req)
	if err != nil {
		return nil, errors.Wrap(err, "failed to generate PoolCreate request")
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolCreate(ctx, pbReq)
	})

	rpcClient.Debugf("Create DAOS pool request: %+v\n", req)
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	msResp, err := ur.getMSResponse()
	if err != nil {
		return nil, errors.Wrap(err, "pool create failed")
	}
	rpcClient.Debugf("Create DAOS pool response: %+v\n", msResp)

	pbPcr, ok := msResp.(*mgmtpb.PoolCreateResp)
	if !ok {
		return nil, errors.New("unable to extract PoolCreateResp from MS response")
	}

	return &PoolCreateResp{
		UUID:    pbReq.Uuid,
		SvcReps: pbPcr.GetSvcreps(),
	}, nil
}

// PoolDestroyReq contains the parameters for a pool destroy request.
type PoolDestroyReq struct {
	msRequest
	unaryRequest
	UUID  string
	Force bool
}

// PoolDestroy performs a pool destroy operation on a DAOS Management Server instance.
func PoolDestroy(ctx context.Context, rpcClient UnaryInvoker, req *PoolDestroyReq) error {
	if err := checkUUID(req.UUID); err != nil {
		return err
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolDestroy(ctx, &mgmtpb.PoolDestroyReq{
			Uuid:  req.UUID,
			Force: req.Force,
		})
	})

	rpcClient.Debugf("Destroy DAOS pool request: %v\n", req)
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	msResp, err := ur.getMSResponse()
	if err != nil {
		return errors.Wrap(err, "pool destroy failed")
	}
	rpcClient.Debugf("Destroy DAOS pool response: %s\n", msResp)

	return nil
}

type (
	// PoolQueryReq contains the parameters for a pool query request.
	PoolQueryReq struct {
		msRequest
		unaryRequest
		UUID string
	}

	// StorageUsageStats represents DAOS storage usage statistics.
	StorageUsageStats struct {
		Total uint64
		Free  uint64
		Min   uint64
		Max   uint64
		Mean  uint64
	}

	// PoolRebuildState indicates the current state of the pool rebuild process.
	PoolRebuildState uint

	// PoolRebuildStatus contains detailed information about the pool rebuild process.
	PoolRebuildStatus struct {
		Status  int32
		State   PoolRebuildState
		Objects uint64
		Records uint64
	}

	// PoolQueryResp contains the pool query response.
	PoolQueryResp struct {
		Status          int32
		UUID            string
		TotalTargets    uint32
		ActiveTargets   uint32
		DisabledTargets uint32
		Rebuild         *PoolRebuildStatus
		Scm             *StorageUsageStats
		Nvme            *StorageUsageStats
	}
)

const (
	// PoolRebuildStateIdle indicates that the rebuild process is idle.
	PoolRebuildStateIdle PoolRebuildState = iota
	// PoolRebuildStateDone indicates that the rebuild process has completed.
	PoolRebuildStateDone
	// PoolRebuildStateBusy indicates that the rebuild process is in progress.
	PoolRebuildStateBusy
)

func (prs PoolRebuildState) String() string {
	return [...]string{"idle", "done", "busy"}[prs]
}

// PoolQuery performs a pool query operation for the specified pool UUID on a
// DAOS Management Server instance.
func PoolQuery(ctx context.Context, rpcClient UnaryInvoker, req *PoolQueryReq) (*PoolQueryResp, error) {
	if err := checkUUID(req.UUID); err != nil {
		return nil, err
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolQuery(ctx, &mgmtpb.PoolQueryReq{Uuid: req.UUID})
	})

	rpcClient.Debugf("Query DAOS pool request: %v\n", req)
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	pqr := new(PoolQueryResp)
	return pqr, convertMSResponse(ur, pqr)
}
