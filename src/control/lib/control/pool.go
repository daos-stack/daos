//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"strconv"
	"strings"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/security/auth"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	// PoolCreateTimeout defines the amount of time a pool create
	// request can take before being timed out.
	PoolCreateTimeout = 10 * time.Minute // be generous for large pools
)

type (
	// Pool contains a unified representation of a DAOS Storage Pool.
	Pool struct {
		// UUID uniquely identifies a pool within the system.
		UUID string
		// ServiceReplicas is the list of ranks on which this pool's
		// service replicas are running.
		ServiceReplicas []system.Rank

		// Info contains information about the pool learned from a
		// query operation.
		Info PoolInfo
	}
)

// checkUUID is a helper function for validating that the supplied
// UUID string parses as a valid UUID.
func checkUUID(uuidStr string) error {
	_, err := uuid.Parse(uuidStr)
	return errors.Wrapf(err, "invalid UUID %q", uuidStr)
}

// formatNameGroup converts system names to principals, If user or group is not
// provided, the effective user and/or effective group will be used.
func formatNameGroup(ext auth.UserExt, usr string, grp string) (string, string, error) {
	if usr == "" || grp == "" {
		eUsr, err := ext.Current()
		if err != nil {
			return "", "", err
		}

		if usr == "" {
			usr = eUsr.Username()
		}
		if grp == "" {
			gid, err := eUsr.Gid()
			if err != nil {
				return "", "", err
			}
			eGrp, err := ext.LookupGroupID(gid)
			if err != nil {
				return "", "", err
			}

			grp = eGrp.Name
		}
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
	in.User, in.UserGroup, err = formatNameGroup(&auth.External{}, in.User, in.UserGroup)
	if err != nil {
		return nil, err
	}

	if in.TotalBytes > 0 && (in.ScmBytes > 0 || in.NvmeBytes > 0) {
		return nil, errors.New("can't mix TotalBytes and ScmBytes/NvmeBytes")
	}
	if in.TotalBytes == 0 && in.ScmBytes == 0 {
		return nil, errors.New("can't create pool with 0 SCM")
	}

	out = new(mgmtpb.PoolCreateReq)
	if err = convert.Types(in, out); err != nil {
		return nil, err
	}

	out.Uuid = uuid.New().String()

	return
}

type (
	// PoolCreateReq contains the parameters for a pool create request.
	PoolCreateReq struct {
		msRequest
		unaryRequest
		retryableRequest
		Label      string
		User       string
		UserGroup  string
		ACL        *AccessControlList
		NumSvcReps uint32
		// auto-config params
		TotalBytes uint64
		ScmRatio   float64
		NumRanks   uint32
		// manual params
		Ranks     []system.Rank
		ScmBytes  uint64
		NvmeBytes uint64
	}

	// PoolCreateResp contains the response from a pool create request.
	PoolCreateResp struct {
		UUID      string   `json:"uuid"`
		SvcReps   []uint32 `json:"svc_reps"`
		TgtRanks  []uint32 `json:"tgt_ranks"`
		ScmBytes  uint64   `json:"scm_bytes"`
		NvmeBytes uint64   `json:"nvme_bytes"`
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
	pbReq.Sys = req.getSystem(rpcClient)
	// TODO: Set this timeout based on the SCM size, when we have a
	// better understanding of the relationship.
	req.SetTimeout(PoolCreateTimeout)
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolCreate(ctx, pbReq)
	})
	req.retryTestFn = func(reqErr error, _ uint) bool {
		switch e := reqErr.(type) {
		case drpc.DaosStatus:
			switch e {
			// These create errors can be retried.
			case drpc.DaosTimedOut, drpc.DaosGroupVersionMismatch,
				drpc.DaosTryAgain:
				return true
			default:
				return false
			}
		default:
			return false
		}
	}

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

	pcr := new(PoolCreateResp)
	pcr.UUID = pbReq.Uuid
	return pcr, convert.Types(pbPcr, pcr)
}

type (
	// PoolResolveIDReq contains the parameters for a request to resolve
	// a human-friendly pool identifier to a UUID.
	PoolResolveIDReq struct {
		msRequest
		unaryRequest
		HumanID string
	}

	// PoolResolveIDResp contains the result of a successful request.
	PoolResolveIDResp struct {
		UUID string
	}
)

// PoolResolveID resolves a user-friendly Pool identifier into a UUID for use
// in subsequent API requests.
func PoolResolveID(ctx context.Context, rpcClient UnaryInvoker, req *PoolResolveIDReq) (*PoolResolveIDResp, error) {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolResolveID(ctx, &mgmtpb.PoolResolveIDReq{
			Sys:     req.getSystem(rpcClient),
			HumanID: req.HumanID,
		})
	})

	rpcClient.Debugf("Resolve DAOS pool ID request: %+v\n", req)
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	prid := new(PoolResolveIDResp)
	if err := convertMSResponse(ur, prid); err != nil {
		return nil, err
	}
	rpcClient.Debugf("Resolve DAOS pool ID response: %s\n", prid)

	return prid, nil
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
			Sys:   req.getSystem(rpcClient),
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

// PoolEvictReq contains the parameters for a pool evict request.
type PoolEvictReq struct {
	msRequest
	unaryRequest
	UUID    string
	Handles []string
}

// PoolEvict performs a pool connection evict operation on a DAOS Management Server instance.
func PoolEvict(ctx context.Context, rpcClient UnaryInvoker, req *PoolEvictReq) error {
	if err := checkUUID(req.UUID); err != nil {
		return err
	}

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolEvict(ctx, &mgmtpb.PoolEvictReq{
			Sys:     req.getSystem(rpcClient),
			Uuid:    req.UUID,
			Handles: req.Handles,
		})
	})

	rpcClient.Debugf("Evict DAOS pool request: %v\n", req)
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	msResp, err := ur.getMSResponse()
	if err != nil {
		return errors.Wrap(err, "pool evict failed")
	}
	rpcClient.Debugf("Evict DAOS pool response: %s\n", msResp)

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
		Total uint64 `json:"total"`
		Free  uint64 `json:"free"`
		Min   uint64 `json:"min"`
		Max   uint64 `json:"max"`
		Mean  uint64 `json:"mean"`
	}

	// PoolRebuildState indicates the current state of the pool rebuild process.
	PoolRebuildState int32

	// PoolRebuildStatus contains detailed information about the pool rebuild process.
	PoolRebuildStatus struct {
		Status  int32            `json:"status"`
		State   PoolRebuildState `json:"state"`
		Objects uint64           `json:"objects"`
		Records uint64           `json:"records"`
	}

	// PoolInfo contains information about the pool.
	PoolInfo struct {
		TotalTargets    uint32             `json:"total_targets"`
		ActiveTargets   uint32             `json:"active_targets"`
		TotalNodes      uint32             `json:"total_nodes"`
		DisabledTargets uint32             `json:"disabled_targets"`
		Version         uint32             `json:"version"`
		Leader          uint32             `json:"leader"`
		Rebuild         *PoolRebuildStatus `json:"rebuild"`
		Scm             *StorageUsageStats `json:"scm"`
		Nvme            *StorageUsageStats `json:"nvme"`
	}

	// PoolQueryResp contains the pool query response.
	PoolQueryResp struct {
		Status int32  `json:"status"`
		UUID   string `json:"uuid"`
		PoolInfo
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
	return strings.ToLower(mgmtpb.PoolRebuildStatus_State_name[int32(prs)])
}

func (prs PoolRebuildState) MarshalJSON() ([]byte, error) {
	stateStr, ok := mgmtpb.PoolRebuildStatus_State_name[int32(prs)]
	if !ok {
		return nil, errors.Errorf("invalid rebuild state %d", prs)
	}
	return []byte(`"` + strings.ToLower(stateStr) + `"`), nil
}

func (prs *PoolRebuildState) UnmarshalJSON(data []byte) error {
	stateStr := strings.ToUpper(string(data))
	state, ok := mgmtpb.PoolRebuildStatus_State_value[stateStr]
	if !ok {
		// Try converting the string to an int32, to handle the
		// conversion from protobuf message using convert.Types().
		si, err := strconv.ParseInt(stateStr, 0, 32)
		if err != nil {
			return errors.Errorf("invalid rebuild state %q", stateStr)
		}

		if _, ok = mgmtpb.PoolRebuildStatus_State_name[int32(si)]; !ok {
			return errors.Errorf("invalid rebuild state %q", stateStr)
		}
		state = int32(si)
	}
	*prs = PoolRebuildState(state)

	return nil
}

// PoolQuery performs a pool query operation for the specified pool UUID on a
// DAOS Management Server instance.
func PoolQuery(ctx context.Context, rpcClient UnaryInvoker, req *PoolQueryReq) (*PoolQueryResp, error) {
	if err := checkUUID(req.UUID); err != nil {
		return nil, err
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolQuery(ctx, &mgmtpb.PoolQueryReq{
			Sys:  req.getSystem(rpcClient),
			Uuid: req.UUID,
		})
	})

	rpcClient.Debugf("Query DAOS pool request: %v\n", req)
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	pqr := new(PoolQueryResp)
	return pqr, convertMSResponse(ur, pqr)
}

// PoolSetPropReq contains pool set-prop parameters.
type PoolSetPropReq struct {
	msRequest
	unaryRequest
	// UUID identifies the pool for which this property should be set.
	UUID string
	// Property is always a string representation of the pool property.
	// It will be resolved into the C representation prior to being
	// forwarded over dRPC.
	Property string
	// Value is an approximation of the union in daos_prop_entry.
	// It can be either a string or a uint64. Struct-based properties
	// are not supported via this API.
	Value interface{}
}

// SetString sets the property value to a string.
func (pspr *PoolSetPropReq) SetString(strVal string) {
	pspr.Value = strVal
}

// SetNumber sets the property value to a uint64 number.
func (pspr *PoolSetPropReq) SetNumber(numVal uint64) {
	pspr.Value = numVal
}

// PoolSetPropResp contains the response to a pool set-prop operation.
type PoolSetPropResp struct {
	UUID     string
	Property string `json:"Name"`
	Value    string
}

// PoolSetProp sends a pool set-prop request to the pool service leader.
func PoolSetProp(ctx context.Context, rpcClient UnaryInvoker, req *PoolSetPropReq) (*PoolSetPropResp, error) {
	if err := checkUUID(req.UUID); err != nil {
		return nil, err
	}

	if req.Property == "" {
		return nil, errors.Errorf("invalid property name %q", req.Property)
	}

	pbReq := &mgmtpb.PoolSetPropReq{
		Sys:  req.getSystem(rpcClient),
		Uuid: req.UUID,
	}
	pbReq.SetPropertyName(req.Property)

	switch val := req.Value.(type) {
	case string:
		pbReq.SetValueString(val)
	case uint64:
		pbReq.SetValueNumber(val)
	default:
		return nil, errors.Errorf("unhandled property value: %+v", req.Value)
	}

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolSetProp(ctx, pbReq)
	})

	rpcClient.Debugf("Query DAOS pool set-prop request: %v\n", req)
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	msResp, err := ur.getMSResponse()
	if err != nil {
		return nil, err
	}

	pbResp, ok := msResp.(*mgmtpb.PoolSetPropResp)
	if !ok {
		return nil, errors.New("unable to extract PoolSetPropResp from MS response")
	}

	pspr := &PoolSetPropResp{
		UUID:     req.UUID,
		Property: pbResp.GetName(),
	}

	switch v := pbResp.GetValue().(type) {
	case *mgmtpb.PoolSetPropResp_Strval:
		pspr.Value = v.Strval
	case *mgmtpb.PoolSetPropResp_Numval:
		pspr.Value = strconv.FormatUint(v.Numval, 10)
	default:
		return nil, errors.Errorf("unable to represent response value %+v", pbResp.Value)
	}

	return pspr, nil
}

// PoolExcludeReq struct contains request
type PoolExcludeReq struct {
	unaryRequest
	msRequest
	UUID      string
	Rank      system.Rank
	Targetidx []uint32
}

// ExcludeResp has no other parameters other than success/failure for now.

// PoolExclude will set a pool target for a specific rank to down.
// This should automatically start the rebuildiing process.
// Returns an error (including any DER code from DAOS).
func PoolExclude(ctx context.Context, rpcClient UnaryInvoker, req *PoolExcludeReq) error {
	if err := checkUUID(req.UUID); err != nil {
		return err
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolExclude(ctx, &mgmtpb.PoolExcludeReq{
			Sys:       req.getSystem(rpcClient),
			Uuid:      req.UUID,
			Rank:      req.Rank.Uint32(),
			Targetidx: req.Targetidx,
		})
	})

	rpcClient.Debugf("Exclude DAOS pool target request: %v\n", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	msResp, err := ur.getMSResponse()
	if err != nil {
		return errors.Wrap(err, "pool Exclude failed")
	}
	rpcClient.Debugf("Exclude DAOS pool target response: %s\n", msResp)

	return nil
}

// PoolDrainReq struct contains request
type PoolDrainReq struct {
	unaryRequest
	msRequest
	UUID      string
	Rank      system.Rank
	Targetidx []uint32
}

// DrainResp has no other parameters other than success/failure for now.

// PoolDrain will set a pool target for a specific rank to the drain status.
// This should automatically start the rebuildiing process.
// Returns an error (including any DER code from DAOS).
func PoolDrain(ctx context.Context, rpcClient UnaryInvoker, req *PoolDrainReq) error {
	if err := checkUUID(req.UUID); err != nil {
		return err
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolDrain(ctx, &mgmtpb.PoolDrainReq{
			Sys:       req.getSystem(rpcClient),
			Uuid:      req.UUID,
			Rank:      req.Rank.Uint32(),
			Targetidx: req.Targetidx,
		})
	})

	rpcClient.Debugf("Drain DAOS pool target request: %v\n", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	msResp, err := ur.getMSResponse()
	if err != nil {
		return errors.Wrap(err, "pool Drain failed")
	}
	rpcClient.Debugf("Drain DAOS pool target response: %s\n", msResp)

	return nil
}

func genPoolExtendRequest(in *PoolExtendReq) (out *mgmtpb.PoolExtendReq, err error) {
	out = new(mgmtpb.PoolExtendReq)
	if err = convert.Types(in, out); err != nil {
		return nil, err
	}

	return
}

// PoolExtendReq struct contains request
type PoolExtendReq struct {
	unaryRequest
	msRequest
	UUID  string
	Ranks []system.Rank
	// TEMP SECTION
	ScmBytes  uint64
	NvmeBytes uint64
	// END TEMP SECTION
}

// PoolExtend will extend the DAOS pool by the specified ranks.
// This should automatically start the rebalance process.
// Returns an error (including any DER code from DAOS).
func PoolExtend(ctx context.Context, rpcClient UnaryInvoker, req *PoolExtendReq) error {
	pbReq, err := genPoolExtendRequest(req)
	if err != nil {
		return errors.Wrap(err, "failed to generate PoolExtend request")
	}
	pbReq.Sys = req.getSystem(rpcClient)

	if err := checkUUID(req.UUID); err != nil {
		return err
	}

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolExtend(ctx, pbReq)
	})

	rpcClient.Debugf("Extend DAOS pool request: %v\n", req)
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	msResp, err := ur.getMSResponse()
	if err != nil {
		return errors.Wrap(err, "pool extend failed")
	}
	rpcClient.Debugf("Extend DAOS pool response: %s\n", msResp)

	return nil
}

// PoolReintegrateReq struct contains request
type PoolReintegrateReq struct {
	unaryRequest
	msRequest
	UUID      string
	Rank      system.Rank
	Targetidx []uint32
}

// ReintegrateResp has no other parameters other than success/failure for now.

// PoolReintegrate will set a pool target for a specific rank back to up.
// This should automatically start the reintegration process.
// Returns an error (including any DER code from DAOS).
func PoolReintegrate(ctx context.Context, rpcClient UnaryInvoker, req *PoolReintegrateReq) error {
	if err := checkUUID(req.UUID); err != nil {
		return err
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolReintegrate(ctx, &mgmtpb.PoolReintegrateReq{
			Sys:       req.getSystem(rpcClient),
			Uuid:      req.UUID,
			Rank:      req.Rank.Uint32(),
			Targetidx: req.Targetidx,
		})
	})

	rpcClient.Debugf("Reintegrate DAOS pool target request: %v\n", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	msResp, err := ur.getMSResponse()
	if err != nil {
		return errors.Wrap(err, "pool reintegrate failed")
	}
	rpcClient.Debugf("Reintegrate DAOS pool target response: %s\n", msResp)

	return nil
}
