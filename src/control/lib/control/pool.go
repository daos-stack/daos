//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"encoding/json"
	"fmt"
	"math"
	"strconv"
	"strings"
	"time"

	"github.com/dustin/go-humanize"
	"github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security/auth"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	// PoolCreateTimeout defines the amount of time a pool create
	// request can take before being timed out.
	PoolCreateTimeout = 10 * time.Minute // be generous for large pools
	// DefaultPoolTimeout is the default timeout for a pool request.
	DefaultPoolTimeout = drpc.DefaultCartTimeout * 3
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

func convertPoolProps(in []*PoolProperty, setProp bool) ([]*mgmtpb.PoolProperty, error) {
	out := make([]*mgmtpb.PoolProperty, len(in))
	allProps := PoolProperties()

	for i, prop := range in {
		if prop == nil {
			return nil, errors.New("nil property")
		}
		out[i] = &mgmtpb.PoolProperty{
			Number: prop.Number,
		}

		// Perform one last set of validations, belt-and-suspenders
		// to guard against a manually-created request with bad
		// properties.
		p, err := allProps.GetProperty(prop.Name)
		if err != nil {
			return nil, err
		}
		if setProp {
			if err := p.SetValue(prop.StringValue()); err != nil {
				return nil, err
			}
			if p.String() != prop.String() {
				return nil, errors.Errorf("%s: unexpected key/val", prop)
			}
		}

		switch val := prop.Value.data.(type) {
		case string:
			out[i].SetValueString(val)
		case uint64:
			out[i].SetValueNumber(val)
		case nil:
			continue
		default:
			return nil, errors.Errorf("unhandled property value: %+v", prop.Value.data)
		}
	}

	return out, nil
}

func (pcr *PoolCreateReq) MarshalJSON() ([]byte, error) {
	props, err := convertPoolProps(pcr.Properties, true)
	if err != nil {
		return nil, err
	}

	type toJSON PoolCreateReq
	return json.Marshal(struct {
		Properties []*mgmtpb.PoolProperty `json:"properties"`
		*toJSON
	}{
		Properties: props,
		toJSON:     (*toJSON)(pcr),
	})
}

// request, filling in any missing fields with reasonable defaults.
func genPoolCreateRequest(in *PoolCreateReq) (out *mgmtpb.PoolCreateReq, err error) {
	// ensure pool ownership is set up correctly
	in.User, in.UserGroup, err = formatNameGroup(&auth.External{}, in.User, in.UserGroup)
	if err != nil {
		return
	}

	if len(in.TierBytes) > 0 {
		if in.TotalBytes > 0 {
			return nil, errors.New("can't mix TotalBytes and ScmBytes/NvmeBytes")
		}
		if in.TotalBytes == 0 && in.TierBytes[0] == 0 {
			return nil, errors.New("can't create pool with 0 SCM")
		}
	} else if in.TotalBytes == 0 {
		return nil, errors.New("can't create pool with size of 0")
	}

	out = new(mgmtpb.PoolCreateReq)
	if err = convert.Types(in, out); err != nil {
		return
	}

	out.Uuid = uuid.New().String()
	return
}

type (
	// poolRequest is an embeddable struct containing methods common
	// to all pool requests.
	poolRequest struct {
		msRequest
		unaryRequest
		retryableRequest
	}
)

func (r *poolRequest) getDeadline() time.Time {
	// If the request has a custom deadline, use that.
	if !r.deadline.IsZero() {
		return r.deadline
	}
	return time.Now().Add(DefaultPoolTimeout)
}

func (r *poolRequest) canRetry(reqErr error, try uint) bool {
	// If the request has set a custom retry test function, use it.
	if r.retryTestFn != nil {
		return r.retryTestFn(reqErr, try)
	}

	// Otherwise, apply a default retry test to all pool requests.
	switch e := reqErr.(type) {
	case drpc.DaosStatus:
		switch e {
		// These pool errors can be retried.
		case drpc.DaosTimedOut, drpc.DaosGroupVersionMismatch,
			drpc.DaosTryAgain, drpc.DaosOutOfGroup, drpc.DaosUnreachable,
			drpc.DaosExcluded:
			return true
		default:
			return false
		}
	default:
		return false
	}
}

type (
	// PoolCreateReq contains the parameters for a pool create request.
	PoolCreateReq struct {
		poolRequest
		User       string
		UserGroup  string
		ACL        *AccessControlList
		NumSvcReps uint32
		Properties []*PoolProperty `json:"-"`
		// auto-config params
		TotalBytes uint64
		TierRatio  []float64
		NumRanks   uint32
		// manual params
		Ranks     []system.Rank
		TierBytes []uint64
	}

	// PoolCreateResp contains the response from a pool create request.
	PoolCreateResp struct {
		UUID      string   `json:"uuid"`
		SvcReps   []uint32 `json:"svc_reps"`
		TgtRanks  []uint32 `json:"tgt_ranks"`
		TierBytes []uint64 `json:"tier_bytes"`
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

	rpcClient.Debugf("Create DAOS pool request: %+v\n", req)
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	msResp, err := ur.getMSResponse()
	if err != nil {
		return nil, errors.Wrap(err, "pool create failed")
	}
	rpcClient.Debugf("Create DAOS pool response: %s\n", mgmtpb.Debug(msResp))

	pbPcr, ok := msResp.(*mgmtpb.PoolCreateResp)
	if !ok {
		return nil, errors.New("unable to extract PoolCreateResp from MS response")
	}

	pcr := new(PoolCreateResp)
	pcr.UUID = pbReq.Uuid

	return pcr, convert.Types(pbPcr, pcr)
}

// PoolDestroyReq contains the parameters for a pool destroy request.
type PoolDestroyReq struct {
	poolRequest
	ID    string
	Force bool
}

// PoolDestroy performs a pool destroy operation on a DAOS Management Server instance.
func PoolDestroy(ctx context.Context, rpcClient UnaryInvoker, req *PoolDestroyReq) error {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolDestroy(ctx, &mgmtpb.PoolDestroyReq{
			Sys:   req.getSystem(rpcClient),
			Id:    req.ID,
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

// PoolUpgradeReq contains the parameters for a pool upgrade request.
type PoolUpgradeReq struct {
	poolRequest
	ID string
}

// PoolUpgrade performs a pool upgrade operation on a DAOS Management Server instance.
func PoolUpgrade(ctx context.Context, rpcClient UnaryInvoker, req *PoolUpgradeReq) error {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolUpgrade(ctx, &mgmtpb.PoolUpgradeReq{
			Sys: req.getSystem(rpcClient),
			Id:  req.ID,
		})
	})

	rpcClient.Debugf("Upgrade DAOS pool request: %v\n", req)
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	msResp, err := ur.getMSResponse()
	if err != nil {
		return errors.Wrap(err, "pool upgrade failed")
	}
	rpcClient.Debugf("Upgrade DAOS pool response: %s\n", msResp)

	return nil
}

// PoolEvictReq contains the parameters for a pool evict request.
type PoolEvictReq struct {
	poolRequest
	ID      string
	Handles []string
}

// PoolEvict performs a pool connection evict operation on a DAOS Management Server instance.
func PoolEvict(ctx context.Context, rpcClient UnaryInvoker, req *PoolEvictReq) error {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolEvict(ctx, &mgmtpb.PoolEvictReq{
			Sys:     req.getSystem(rpcClient),
			Id:      req.ID,
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
		poolRequest
		ID                   string
		IncludeEnabledRanks  bool
		IncludeDisabledRanks bool
	}

	// StorageUsageStats represents DAOS storage usage statistics.
	StorageUsageStats struct {
		Total     uint64 `json:"total"`
		Free      uint64 `json:"free"`
		Min       uint64 `json:"min"`
		Max       uint64 `json:"max"`
		Mean      uint64 `json:"mean"`
		MediaType string `json:"media_type"`
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
		TotalTargets    uint32               `json:"total_targets"`
		ActiveTargets   uint32               `json:"active_targets"`
		TotalEngines    uint32               `json:"total_engines"`
		DisabledTargets uint32               `json:"disabled_targets"`
		Version         uint32               `json:"version"`
		Leader          uint32               `json:"leader"`
		Rebuild         *PoolRebuildStatus   `json:"rebuild"`
		TierStats       []*StorageUsageStats `json:"tier_stats"`
		EnabledRanks    *system.RankSet      `json:"-"`
		DisabledRanks   *system.RankSet      `json:"-"`
	}

	// PoolQueryResp contains the pool query response.
	PoolQueryResp struct {
		Status int32  `json:"status"`
		UUID   string `json:"uuid"`
		PoolInfo
	}
)

func (pqr *PoolQueryResp) MarshalJSON() ([]byte, error) {
	if pqr == nil {
		return []byte("null"), nil
	}

	type Alias PoolQueryResp
	aux := &struct {
		EnabledRanks  *[]system.Rank `json:"enabled_ranks"`
		DisabledRanks *[]system.Rank `json:"disabled_ranks"`
		*Alias
	}{
		Alias: (*Alias)(pqr),
	}

	if pqr.EnabledRanks != nil {
		ranks := pqr.EnabledRanks.Ranks()
		aux.EnabledRanks = &ranks
	}

	if pqr.DisabledRanks != nil {
		ranks := pqr.DisabledRanks.Ranks()
		aux.DisabledRanks = &ranks
	}

	return json.Marshal(&aux)
}

func unmarshallRankSet(ranks string) (*system.RankSet, error) {
	switch ranks {
	case "":
		return nil, nil
	case "[]":
		return &system.RankSet{}, nil
	default:
		return system.CreateRankSet(ranks)
	}
}

func (pqr *PoolQueryResp) UnmarshalJSON(data []byte) error {
	type Alias PoolQueryResp
	aux := &struct {
		EnabledRanks  string `json:"enabled_ranks"`
		DisabledRanks string `json:"disabled_ranks"`
		*Alias
	}{
		Alias: (*Alias)(pqr),
	}

	if err := json.Unmarshal(data, &aux); err != nil {
		return err
	}

	if rankSet, err := unmarshallRankSet(aux.EnabledRanks); err != nil {
		return err
	} else {
		pqr.EnabledRanks = rankSet
	}

	if rankSet, err := unmarshallRankSet(aux.DisabledRanks); err != nil {
		return err
	} else {
		pqr.DisabledRanks = rankSet
	}

	return nil
}

func (sus *StorageUsageStats) UnmarshalJSON(data []byte) error {
	type fromJSON StorageUsageStats
	from := &struct {
		MediaType uint32 `json:"media_type"`
		*fromJSON
	}{
		fromJSON: (*fromJSON)(sus),
	}

	if err := json.Unmarshal(data, from); err != nil {
		return err
	}

	switch from.MediaType {
	case drpc.MediaTypeScm:
		sus.MediaType = "scm"
	case drpc.MediaTypeNvme:
		sus.MediaType = "nvme"
	default:
		sus.MediaType = "unknown"
	}

	return nil
}

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

// PoolQuery performs a pool query operation for the specified pool ID on a
// DAOS Management Server instance.
func PoolQuery(ctx context.Context, rpcClient UnaryInvoker, req *PoolQueryReq) (*PoolQueryResp, error) {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolQuery(ctx, &mgmtpb.PoolQueryReq{
			Sys:                  req.getSystem(rpcClient),
			Id:                   req.ID,
			IncludeEnabledRanks:  req.IncludeEnabledRanks,
			IncludeDisabledRanks: req.IncludeDisabledRanks,
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
	poolRequest
	// ID identifies the pool for which this property should be set.
	ID         string
	Properties []*PoolProperty
}

// PoolSetProp sends a pool set-prop request to the pool service leader.
func PoolSetProp(ctx context.Context, rpcClient UnaryInvoker, req *PoolSetPropReq) error {
	if req == nil {
		return errors.Errorf("nil %T in PoolSetProp()", req)
	}
	if len(req.Properties) == 0 {
		return errors.New("empty properties list in PoolSetProp()")
	}

	pbReq := &mgmtpb.PoolSetPropReq{
		Sys: req.getSystem(rpcClient),
		Id:  req.ID,
	}

	var err error
	pbReq.Properties, err = convertPoolProps(req.Properties, true)
	if err != nil {
		return err
	}

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolSetProp(ctx, pbReq)
	})

	rpcClient.Debugf("DAOS pool set-prop request: %+v\n", pbReq)
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	msResp, err := ur.getMSResponse()
	if err != nil {
		return err
	}
	rpcClient.Debugf("pool set-prop response: %s\n", msResp)

	return nil
}

// PoolGetPropReq contains pool get-prop parameters.
type PoolGetPropReq struct {
	poolRequest
	// ID identifies the pool for which this property should be set.
	ID string
	// Name is always a string representation of the pool property.
	// It will be resolved into the C representation prior to being
	// forwarded over dRPC. If not set, all properties will be returned.
	Name string
	// Properties is the list of properties to be retrieved. If empty,
	// all properties will be retrieved.
	Properties []*PoolProperty
}

// PoolGetProp sends a pool get-prop request to the pool service leader.
func PoolGetProp(ctx context.Context, rpcClient UnaryInvoker, req *PoolGetPropReq) ([]*PoolProperty, error) {
	// Get all by default.
	if len(req.Properties) == 0 {
		allProps := PoolProperties()
		req.Properties = make([]*PoolProperty, 0, len(allProps))
		for _, key := range allProps.Keys() {
			hdlr := allProps[key]
			req.Properties = append(req.Properties, hdlr.GetProperty(key))
		}
	}

	pbReq := &mgmtpb.PoolGetPropReq{
		Sys: req.getSystem(rpcClient),
		Id:  req.ID,
	}
	var err error
	pbReq.Properties, err = convertPoolProps(req.Properties, false)
	if err != nil {
		return nil, err
	}

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolGetProp(ctx, pbReq)
	})

	rpcClient.Debugf("pool get-prop request: %+v\n", req)
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	msResp, err := ur.getMSResponse()
	if err != nil {
		return nil, err
	}
	pbResp, ok := msResp.(*mgmtpb.PoolGetPropResp)
	if !ok {
		return nil, errors.New("unable to extract PoolGetPropResp from MS response")
	}

	resp := req.Properties
	pbMap := make(map[uint32]*mgmtpb.PoolProperty)
	for _, prop := range pbResp.GetProperties() {
		if _, found := pbMap[prop.GetNumber()]; found {
			return nil, errors.Errorf("got > 1 %d in response", prop.GetNumber())
		}
		pbMap[prop.GetNumber()] = prop
	}

	for _, prop := range resp {
		pbProp, found := pbMap[prop.Number]
		if !found {
			continue
		}
		switch v := pbProp.GetValue().(type) {
		case *mgmtpb.PoolProperty_Strval:
			prop.Value.SetString(v.Strval)
		case *mgmtpb.PoolProperty_Numval:
			prop.Value.SetNumber(v.Numval)
		default:
			return nil, errors.Errorf("unable to represent response value %+v", v)
		}
	}

	rpcClient.Debugf("pool get-prop resp: %+v\n", resp)

	return resp, nil
}

// PoolExcludeReq struct contains request
type PoolExcludeReq struct {
	poolRequest
	ID        string
	Rank      system.Rank
	Targetidx []uint32
}

// ExcludeResp has no other parameters other than success/failure for now.

// PoolExclude will set a pool target for a specific rank to down.
// This should automatically start the rebuildiing process.
// Returns an error (including any DER code from DAOS).
func PoolExclude(ctx context.Context, rpcClient UnaryInvoker, req *PoolExcludeReq) error {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolExclude(ctx, &mgmtpb.PoolExcludeReq{
			Sys:       req.getSystem(rpcClient),
			Id:        req.ID,
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
	poolRequest
	ID        string
	Rank      system.Rank
	Targetidx []uint32
}

// DrainResp has no other parameters other than success/failure for now.

// PoolDrain will set a pool target for a specific rank to the drain status.
// This should automatically start the rebuildiing process.
// Returns an error (including any DER code from DAOS).
func PoolDrain(ctx context.Context, rpcClient UnaryInvoker, req *PoolDrainReq) error {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolDrain(ctx, &mgmtpb.PoolDrainReq{
			Sys:       req.getSystem(rpcClient),
			Id:        req.ID,
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
	poolRequest
	ID    string
	Ranks []system.Rank
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
	poolRequest
	ID        string
	Rank      system.Rank
	Targetidx []uint32
}

// ReintegrateResp has no other parameters other than success/failure for now.

// PoolReintegrate will set a pool target for a specific rank back to up.
// This should automatically start the reintegration process.
// Returns an error (including any DER code from DAOS).
func PoolReintegrate(ctx context.Context, rpcClient UnaryInvoker, req *PoolReintegrateReq) error {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolReintegrate(ctx, &mgmtpb.PoolReintegrateReq{
			Sys:       req.getSystem(rpcClient),
			Id:        req.ID,
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

type (
	// PoolTierUsage describes usage of a single pool storage tier.
	PoolTierUsage struct {
		// TierName identifies a pool's storage tier.
		TierName string `json:"tier_name"`
		// Size is the total number of bytes in the pool tier.
		Size uint64 `json:"size"`
		// Free is the number of free bytes in the pool tier.
		Free uint64 `json:"free"`
		// Imbalance is the percentage imbalance of pool tier usage
		// across all the targets.
		Imbalance uint32 `json:"imbalance"`
	}

	// Pool contains a representation of a DAOS Storage Pool including usage
	// statistics.
	Pool struct {
		// UUID uniquely identifies a pool within the system.
		UUID string `json:"uuid"`
		// Label is an optional human-friendly identifier for a pool.
		Label string `json:"label,omitempty"`
		// ServiceReplicas is the list of ranks on which this pool's
		// service replicas are running.
		ServiceReplicas []system.Rank `json:"svc_reps"`
		// State is the current state of the pool.
		State string `json:"state"`

		// TargetsTotal is the total number of targets in pool.
		TargetsTotal uint32 `json:"targets_total"`
		// TargetsDisabled is the number of inactive targets in pool.
		TargetsDisabled uint32 `json:"targets_disabled"`

		// QueryErrorMsg reports an RPC error returned from a query.
		QueryErrorMsg string `json:"query_error_msg"`
		// QueryStatusMsg reports any DAOS error returned from a query
		// operation converted into human readable message.
		QueryStatusMsg string `json:"query_status_msg"`

		// Usage contains pool usage statistics for each storage tier.
		Usage []*PoolTierUsage `json:"usage"`
	}
)

func (p *Pool) setUsage(pqr *PoolQueryResp) {
	for idx, tu := range pqr.TierStats {
		spread := tu.Max - tu.Min
		imbalance := float64(spread) / (float64(tu.Total) / float64(pqr.ActiveTargets))

		tn := "NVME"
		if idx == 0 {
			tn = "SCM"
		}

		p.Usage = append(p.Usage,
			&PoolTierUsage{
				TierName:  tn,
				Size:      tu.Total,
				Free:      tu.Free,
				Imbalance: uint32(imbalance * 100),
			},
		)
	}
}

// HasErrors indicates whether a pool query operation failed on this pool.
func (p *Pool) HasErrors() bool {
	return p.QueryErrorMsg != "" || p.QueryStatusMsg != ""
}

// GetPool retrieves effective name for pool from either label or UUID.
func (p *Pool) GetName() string {
	name := p.Label
	if name == "" {
		// use short version of uuid if no label
		name = strings.Split(p.UUID, "-")[0]
	}
	return name
}

// ListPoolsReq contains the inputs for the list pools command.
type ListPoolsReq struct {
	unaryRequest
	msRequest
	NoQuery bool
}

// ListPoolsResp contains the status of the request and, if successful, the list
// of pools in the system.
type ListPoolsResp struct {
	Status int32   `json:"status"`
	Pools  []*Pool `json:"pools"`
}

// Validate returns error if response contents are unexpected, string of
// warnings if pool queries have failed or nil values if contents are expected.
func (lpr *ListPoolsResp) Validate() (string, error) {
	var numTiers int
	out := new(strings.Builder)

	for i, p := range lpr.Pools {
		if p.UUID == "" {
			return "", errors.Errorf("pool with index %d has no uuid", i)
		}
		if p.QueryErrorMsg != "" {
			fmt.Fprintf(out, "Query on pool %q unsuccessful, error: %q\n",
				p.GetName(), p.QueryErrorMsg)
			continue // no usage stats expected
		}
		if p.QueryStatusMsg != "" {
			fmt.Fprintf(out, "Query on pool %q unsuccessful, status: %q\n",
				p.GetName(), p.QueryStatusMsg)
			continue // no usage stats expected
		}
		if len(p.Usage) == 0 {
			continue // no usage stats in response
		}
		if numTiers != 0 && len(p.Usage) != numTiers {
			return "", errors.Errorf("pool %s has %d storage tiers, want %d",
				p.UUID, len(p.Usage), numTiers)
		}
		numTiers = len(p.Usage)
	}

	return out.String(), nil
}

// Errors returns summary of response errors including pool query failures.
func (lpr *ListPoolsResp) Errors() error {
	warn, err := lpr.Validate()
	if err != nil {
		return err
	}
	if warn != "" {
		return errors.New(warn)
	}
	return nil
}

// ListPools fetches the list of all pools and their service replicas from the
// system.
func ListPools(ctx context.Context, rpcClient UnaryInvoker, req *ListPoolsReq) (*ListPoolsResp, error) {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).ListPools(ctx, &mgmtpb.ListPoolsReq{
			Sys: req.getSystem(rpcClient),
		})
	})
	rpcClient.Debugf("DAOS system list-pools request: %+v", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(ListPoolsResp)
	if err := convertMSResponse(ur, resp); err != nil {
		return nil, err
	}

	if req.NoQuery {
		return resp, nil
	}

	// issue query request and populate usage statistics for each pool
	for _, p := range resp.Pools {
		if p.State != system.PoolServiceStateReady.String() {
			rpcClient.Debugf("Skipping query of pool in state: %s", p.State)
			continue
		}
		rpcClient.Debugf("Fetching details for discovered pool: %v", p)

		resp, err := PoolQuery(ctx, rpcClient, &PoolQueryReq{ID: p.UUID})
		if err != nil {
			p.QueryErrorMsg = err.Error()
			if p.QueryErrorMsg == "" {
				p.QueryErrorMsg = "unknown error"
			}
			continue
		}
		if resp.Status != 0 {
			p.QueryStatusMsg = drpc.DaosStatus(resp.Status).Error()
			if p.QueryStatusMsg == "" {
				p.QueryStatusMsg = "unknown error"
			}
			continue
		}
		if p.UUID != resp.UUID {
			return nil, errors.New("pool query response uuid does not match request")
		}

		p.TargetsTotal = resp.TotalTargets
		p.TargetsDisabled = resp.DisabledTargets
		p.setUsage(resp)
	}

	for _, p := range resp.Pools {
		rpcClient.Debugf("DAOS system pool in list-pools response: %+v", p)
	}

	return resp, nil
}

// Return the maximal SCM and NVMe size of a pool which could be created with all the storage nodes.
//
// TODO (DAOS-9557) This function should takes an extra parameter to filter the engine ranks to use.
func GetMaxPoolSize(ctx context.Context, log logging.Logger, rpcClient UnaryInvoker) (uint64, uint64, error) {
	storageScanReq := &StorageScanReq{Usage: true}
	resp, err := StorageScan(ctx, rpcClient, storageScanReq)
	if err != nil {
		return 0, 0, err
	}

	if len(resp.HostStorage) == 0 {
		return 0, 0, errors.New("No DAOS server available")
	}

	hostStorageMap := resp.HostStorage
	var scmBytes uint64 = math.MaxUint64
	var nvmeBytes uint64 = math.MaxUint64
	for _, key := range hostStorageMap.Keys() {
		hostStorageSet := hostStorageMap[key]
		hostStorage := hostStorageSet.HostStorage

		if hostStorage.ScmNamespaces.Free() == uint64(0) {
			return 0, 0, errors.Errorf("Host without SCM storage: hostname=%s",
				hostStorageSet.HostSet.String())
		}

		// FIXME (DAOS-9557) At this time the rank associated to one namespace is not
		// defined in the StorageScanResp.  Thus we rely on the hypothesis that each SCM
		// namespace is associated to one and only one rank. Eventually, the protocol should
		// be changed to define if a SCM namespace is associated with one rank or not. If
		// yes, it should define with which rank the SCM namespace is associated.
		for _, scmNamespace := range hostStorage.ScmNamespaces {
			if scmNamespace.Mount == nil {
				continue
			}
			scmNamespaceFreeBytes := scmNamespace.Mount.AvailBytes

			if scmBytes > scmNamespaceFreeBytes {
				scmBytes = scmNamespaceFreeBytes
			}
		}

		nvmeRanksBytes := make(map[system.Rank]uint64, 0)
		for _, nvmeController := range hostStorage.NvmeDevices {
			for _, smdDevice := range nvmeController.SmdDevices {
				if !smdDevice.NvmeState.IsNormal() {
					log.Infof("WARNING: SMD device %s (instance %d, ctrlr %s) "+
						"not usable (device state %q)",
						smdDevice.UUID, smdDevice.Rank, smdDevice.TrAddr,
						smdDevice.NvmeState.String())
					continue
				}

				nvmeRanksBytes[smdDevice.Rank] += smdDevice.AvailBytes
			}
		}
		for _, nvmeRankBytes := range nvmeRanksBytes {
			if nvmeBytes > nvmeRankBytes {
				nvmeBytes = nvmeRankBytes
			}
		}
	}

	if nvmeBytes == math.MaxUint64 {
		nvmeBytes = 0
	}

	// TODO (DAOS-9557) Check if there is no ranks (i.e. rank with SCM available) with some NVMe
	// storage and other without

	rpcClient.Debugf("Maximal size of a pool: scmBytes=%s (%d B) nvmeBytes=%s (%d B)",
		humanize.Bytes(scmBytes), scmBytes, humanize.Bytes(nvmeBytes), nvmeBytes)

	return scmBytes, nvmeBytes, nil
}
