//
// (C) Copyright 2020-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"encoding/json"
	"fmt"
	"math"
	"os/user"
	"sort"
	"strings"
	"time"

	"github.com/dustin/go-humanize"
	"github.com/dustin/go-humanize/english"
	"github.com/google/uuid"
	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	pbUtil "github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	// PoolCreateTimeout defines the amount of time a pool create
	// request can take before being timed out.
	PoolCreateTimeout = 10 * time.Minute // be generous for large pools
	// DefaultPoolTimeout is the default timeout for a pool request.
	DefaultPoolTimeout = 5 * time.Minute
)

// Pool create error conditions.
var (
	errPoolCreateFirstTierZeroBytes = errors.New("can't create pool with 0 byte first tier")
	errPoolCreateFirstTierRatioZero = errors.New("can't create pool with 0.0 first tier ratio")
)

// checkUUID is a helper function for validating that the supplied
// UUID string parses as a valid UUID.
func checkUUID(uuidStr string) error {
	_, err := uuid.Parse(uuidStr)
	return errors.Wrapf(err, "invalid UUID %q", uuidStr)
}

// formatNameGroup converts system names to principals, If user or group is not
// provided, the effective user and/or effective group will be used.
func formatNameGroup(usr string, grp string) (string, string, error) {
	if usr == "" || grp == "" {
		eUsr, err := user.Current()
		if err != nil {
			return "", "", err
		}

		if usr == "" {
			usr = eUsr.Username
		}
		if grp == "" {
			gid := eUsr.Gid
			eGrp, err := user.LookupGroupId(gid)
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

func convertPoolProps(in []*daos.PoolProperty, setProp bool) ([]*mgmtpb.PoolProperty, error) {
	out := make([]*mgmtpb.PoolProperty, len(in))
	allProps := daos.PoolProperties()

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

		if num, err := prop.Value.GetNumber(); err == nil {
			out[i].SetValueNumber(num)
		} else if prop.Value.IsSet() {
			out[i].SetValueString(prop.Value.String())
		} else {
			// not set; just skip it
			continue
		}
	}

	return out, nil
}

func (pcr *PoolCreateReq) MarshalJSON() ([]byte, error) {
	props, err := convertPoolProps(pcr.Properties, true)
	if err != nil {
		return nil, err
	}

	var acl []string
	if pcr.ACL != nil {
		acl = pcr.ACL.Entries
	}

	type toJSON PoolCreateReq
	return json.Marshal(struct {
		UUID       string                 `json:"uuid,omitempty"`
		Properties []*mgmtpb.PoolProperty `json:"properties"`
		ACL        []string               `json:"acl"`
		*toJSON
	}{
		UUID: func() string {
			if pcr.UUID == uuid.Nil {
				return ""
			}
			return pcr.UUID.String()
		}(),
		Properties: props,
		ACL:        acl,
		toJSON:     (*toJSON)(pcr),
	})
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
	r.SetTimeout(DefaultPoolTimeout)
	return r.deadline
}

func (r *poolRequest) canRetry(reqErr error, try uint) bool {
	// If the request has set a custom retry test function, use it.
	if r.retryTestFn != nil {
		return r.retryTestFn(reqErr, try)
	}

	// Otherwise, apply a default retry test to all pool requests.
	switch e := reqErr.(type) {
	case daos.Status:
		switch e {
		// These pool errors can be retried.
		case daos.TimedOut, daos.GroupVersionMismatch,
			daos.TryAgain, daos.OutOfGroup, daos.Unreachable,
			daos.Excluded:
			return true
		default:
			return false
		}
	case *fault.Fault:
		switch e.Code {
		case code.ServerDataPlaneNotStarted, code.SystemPoolLocked:
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
		UUID       uuid.UUID            `json:"uuid,omitempty"` // Optional UUID; auto-generate if not supplied
		User       string               `json:"user"`
		UserGroup  string               `json:"user_group"`
		ACL        *AccessControlList   `json:"-"`
		NumSvcReps uint32               `json:"num_svc_reps"`
		Properties []*daos.PoolProperty `json:"-"`
		TotalBytes uint64               `json:"total_bytes"` // Auto-sizing param
		TierRatio  []float64            `json:"tier_ratio"`  // Auto-sizing param
		NumRanks   uint32               `json:"num_ranks"`   // Auto-sizing param
		Ranks      []ranklist.Rank      `json:"ranks"`       // Manual-sizing param
		TierBytes  []uint64             `json:"tier_bytes"`  // Per-rank values
		MemRatio   float32              `json:"mem_ratio"`   // mem_file_size:meta_blob_size
	}

	// PoolCreateResp contains the response from a pool create request.
	PoolCreateResp struct {
		UUID          string   `json:"uuid"`
		Leader        uint32   `json:"svc_ldr"`
		SvcReps       []uint32 `json:"svc_reps"`
		TgtRanks      []uint32 `json:"tgt_ranks"`
		TierBytes     []uint64 `json:"tier_bytes"`       // Per-rank storage tier sizes.
		MemFileBytes  uint64   `json:"mem_file_bytes"`   // Per-rank. MD-on-SSD mode only.
		MdOnSsdActive bool     `json:"md_on_ssd_active"` // MD-on-SSD mode.
	}
)

type maxPoolSizeGetter func(*PoolCreateReq) (uint64, uint64, error)

func poolCreateReqChkSizes(log debugLogger, getMaxPoolSz maxPoolSizeGetter, req *PoolCreateReq) error {
	hasTotBytes := req.TotalBytes > 0
	hasTierBytes := len(req.TierBytes) == 2
	hasNoTierBytes := len(req.TierBytes) == 0
	hasTierRatio := len(req.TierRatio) == 2
	hasNoTierRatio := len(req.TierRatio) == 0

	switch {
	case hasTierBytes && hasNoTierRatio && !hasTotBytes:
		if req.TierBytes[0] == 0 {
			return errPoolCreateFirstTierZeroBytes
		}
		// Storage sizes have been written to TierBytes in request (manual-size).
		log.Debugf("manual-size pool create mode: %+v", req)

	case hasNoTierBytes && hasTierRatio && hasTotBytes:
		if req.TierRatio[0] == 0 {
			return errPoolCreateFirstTierRatioZero
		}
		// Storage tier ratios and total pool size given, distribution of space across
		// ranks to be calculated on the server side (auto-total-size).
		log.Debugf("auto-total-size pool create mode: %+v", req)

	case hasNoTierBytes && hasTierRatio && !hasTotBytes:
		if req.TierRatio[0] == 0 {
			return errPoolCreateFirstTierRatioZero
		}
		availRatio := req.TierRatio[0]
		if req.TierRatio[1] != availRatio {
			return errors.New("different tier ratios with no total size is not supported")
		}
		req.TierRatio = nil
		// Storage tier ratios specified without a total size, use specified fraction of
		// available space (auto-percentage-size).
		scmBytes, nvmeBytes, err := getMaxPoolSz(req)
		if err != nil {
			return err
		}
		req.TierBytes = []uint64{
			uint64(float64(scmBytes) * availRatio),
			uint64(float64(nvmeBytes) * availRatio),
		}
		if req.TierBytes[0] == 0 {
			return errors.Errorf("Not enough SCM storage available with ratio %d%%: "+
				"SCM storage capacity or ratio should be increased",
				int(availRatio*100))
		}
		log.Debugf("auto-percentage-size pool create mode: %+v", req)

	default:
		return errors.Errorf("unexpected parameters in pool create request: %+v", req)
	}

	return nil
}

func poolCreateGenPBReq(ctx context.Context, rpcClient UnaryInvoker, in *PoolCreateReq) (out *mgmtpb.PoolCreateReq, err error) {
	// ensure pool ownership is set up correctly
	in.User, in.UserGroup, err = formatNameGroup(in.User, in.UserGroup)
	if err != nil {
		return
	}

	getMaxPoolSz := func(createReq *PoolCreateReq) (uint64, uint64, error) {
		return getMaxPoolSize(ctx, rpcClient, createReq)
	}

	if err = poolCreateReqChkSizes(rpcClient, getMaxPoolSz, in); err != nil {
		return
	}

	out = new(mgmtpb.PoolCreateReq)
	if err = convert.Types(in, out); err != nil {
		return
	}

	if out.Uuid == "" {
		out.Uuid = uuid.New().String()
	}
	return
}

// PoolCreate performs a pool create operation on a DAOS Management Server instance.
// Default values for missing request parameters (e.g. owner/group) are generated when
// appropriate.
func PoolCreate(ctx context.Context, rpcClient UnaryInvoker, req *PoolCreateReq) (*PoolCreateResp, error) {
	pbReq, err := poolCreateGenPBReq(ctx, rpcClient, req)
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

	rpcClient.Debugf("Create DAOS pool request: %+v\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	pcr := new(PoolCreateResp)
	if err := convertMSResponse(ur, pcr); err != nil {
		return nil, errors.Wrap(err, "pool create failed")
	}
	if pcr.UUID == "" {
		pcr.UUID = pbReq.Uuid
	}

	return pcr, nil
}

// PoolDestroyReq contains the parameters for a pool destroy request.
type PoolDestroyReq struct {
	poolRequest
	ID        string
	Recursive bool // Remove pool and any child containers.
	Force     bool
}

// PoolDestroy performs a pool destroy operation on a DAOS Management Server instance.
func PoolDestroy(ctx context.Context, rpcClient UnaryInvoker, req *PoolDestroyReq) error {
	pbReq := &mgmtpb.PoolDestroyReq{
		Sys:       req.getSystem(rpcClient),
		Id:        req.ID,
		Recursive: req.Recursive,
		Force:     req.Force,
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolDestroy(ctx, pbReq)
	})

	rpcClient.Debugf("Destroy DAOS pool request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	if err := ur.getMSError(); err != nil {
		// If the error is due to a retried destroy failing to find
		// the pool, then we can assume that the pool was destroyed
		// via a server-side cleanup and we can intercept it. Everything
		// else is still an error.
		if !(ur.retryCount > 0 && system.IsPoolNotFound(err)) {
			return errors.Wrap(err, "pool destroy failed")
		}
	}

	return nil
}

// PoolUpgradeReq contains the parameters for a pool upgrade request.
type PoolUpgradeReq struct {
	poolRequest
	ID string
}

// PoolUpgrade performs a pool upgrade operation on a DAOS Management Server instance.
func PoolUpgrade(ctx context.Context, rpcClient UnaryInvoker, req *PoolUpgradeReq) error {
	pbReq := &mgmtpb.PoolUpgradeReq{
		Sys: req.getSystem(rpcClient),
		Id:  req.ID,
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolUpgrade(ctx, pbReq)
	})

	rpcClient.Debugf("Upgrade DAOS pool request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	return errors.Wrap(ur.getMSError(), "pool upgrade failed")
}

// PoolEvictReq contains the parameters for a pool evict request.
type PoolEvictReq struct {
	poolRequest
	ID      string
	Handles []string
}

// PoolEvict performs a pool connection evict operation on a DAOS Management Server instance.
func PoolEvict(ctx context.Context, rpcClient UnaryInvoker, req *PoolEvictReq) error {
	pbReq := &mgmtpb.PoolEvictReq{
		Sys:     req.getSystem(rpcClient),
		Id:      req.ID,
		Handles: req.Handles,
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolEvict(ctx, pbReq)
	})

	rpcClient.Debugf("Evict DAOS pool request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	return errors.Wrap(ur.getMSError(), "pool evict failed")
}

type (
	// PoolQueryReq contains the parameters for a pool query request.
	PoolQueryReq struct {
		poolRequest
		ID        string
		QueryMask daos.PoolQueryMask
	}

	// PoolQueryResp contains the pool query response.
	PoolQueryResp struct {
		Status int32 `json:"status"`
		daos.PoolInfo
	}

	// PoolQueryTargetReq contains parameters for a pool query target request
	PoolQueryTargetReq struct {
		poolRequest
		ID      string
		Rank    ranklist.Rank
		Targets []uint32
	}

	// PoolQueryTargetResp contains a pool query target response
	PoolQueryTargetResp struct {
		Status int32 `json:"status"`
		Infos  []*daos.PoolQueryTargetInfo
	}
)

func (pqr *PoolQueryResp) MarshalJSON() ([]byte, error) {
	if pqr == nil {
		return []byte("null"), nil
	}

	// Bypass the MarshalJSON() implementation in daos.PoolInfo,
	// which would otherwise be promoted, resulting in the Status
	// field not being included.
	type Alias daos.PoolInfo
	return json.Marshal(&struct {
		*Alias
		Status int32 `json:"status"`
	}{
		Alias:  (*Alias)(&pqr.PoolInfo),
		Status: pqr.Status,
	})
}

// PoolQuery performs a pool query operation for the specified pool ID on a
// DAOS Management Server instance.
func PoolQuery(ctx context.Context, rpcClient UnaryInvoker, req *PoolQueryReq) (*PoolQueryResp, error) {
	return poolQueryInt(ctx, rpcClient, req, nil)
}

// poolQueryInt is the internal implementation of PoolQuery that
// allows the caller to supply an optional pointer to the response
// that will be filled out with the query details.
func poolQueryInt(ctx context.Context, rpcClient UnaryInvoker, req *PoolQueryReq, resp *PoolQueryResp) (*PoolQueryResp, error) {
	pbReq := &mgmtpb.PoolQueryReq{
		Sys:       req.getSystem(rpcClient),
		Id:        req.ID,
		QueryMask: uint64(req.QueryMask),
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolQuery(ctx, pbReq)
	})

	rpcClient.Debugf("Query DAOS pool request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	if resp == nil {
		resp = new(PoolQueryResp)
	}
	if err := convertMSResponse(ur, resp); err != nil {
		return nil, err
	}

	err = resp.UpdateState()
	if err != nil {
		return nil, err
	}

	return resp, err
}

// UpdateState updates the pool state based on response field values.
func (pqr *PoolQueryResp) UpdateState() error {
	// Update the state as Ready if DAOS return code is 0.
	if pqr.Status == 0 {
		pqr.State = daos.PoolServiceStateReady
	}

	// Pool state is unknown, if TotalTargets is 0.
	if pqr.TotalTargets == 0 {
		pqr.State = daos.PoolServiceStateUnknown
	}

	// Update the Pool state as Degraded, if initial state is Ready and any target is disabled
	if pqr.State == daos.PoolServiceStateReady && pqr.DisabledTargets > 0 {
		pqr.State = daos.PoolServiceStateDegraded
	}

	return nil
}

// PoolQueryTargets performs a pool query targets operation on a DAOS Management Server instance,
// for the specified pool ID, pool engine rank, and target indices.
func PoolQueryTargets(ctx context.Context, rpcClient UnaryInvoker, req *PoolQueryTargetReq) (*PoolQueryTargetResp, error) {
	pbReq := &mgmtpb.PoolQueryTargetReq{
		Sys:     req.getSystem(rpcClient),
		Id:      req.ID,
		Rank:    uint32(req.Rank),
		Targets: req.Targets,
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolQueryTarget(ctx, pbReq)
	})

	rpcClient.Debugf("Query DAOS pool targets request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	pqtr := new(PoolQueryTargetResp)

	msResp, err := ur.getMSResponse()
	if err != nil {
		return nil, err
	}

	pbResp, ok := msResp.(*mgmtpb.PoolQueryTargetResp)
	if !ok {
		return nil, errors.New("unable to extract PoolQueryTargetResp from MS response")
	}

	pqtr.Status = pbResp.Status
	for tgt := 0; tgt < len(pbResp.Infos); tgt++ {
		tgtInfo, err := convertPoolTargetInfo(pbResp.Infos[tgt])
		if err != nil {
			return nil, err
		}
		pqtr.Infos = append(pqtr.Infos, tgtInfo)
	}
	return pqtr, nil
}

// For using the pretty printer that dmg uses for this target info.
func convertPoolTargetInfo(pbInfo *mgmtpb.PoolQueryTargetInfo) (*daos.PoolQueryTargetInfo, error) {
	pqti := new(daos.PoolQueryTargetInfo)
	pqti.Type = daos.PoolQueryTargetType(pbInfo.Type)
	pqti.State = daos.PoolQueryTargetState(pbInfo.State)
	pqti.Space = []*daos.StorageUsageStats{
		{
			Total:     uint64(pbInfo.Space[0].Total),
			Free:      uint64(pbInfo.Space[0].Free),
			MediaType: daos.StorageMediaType(pbInfo.Space[0].MediaType),
		},
		{
			Total:     uint64(pbInfo.Space[1].Total),
			Free:      uint64(pbInfo.Space[1].Free),
			MediaType: daos.StorageMediaType(pbInfo.Space[1].MediaType),
		},
	}

	return pqti, nil
}

// PoolSetPropReq contains pool set-prop parameters.
type PoolSetPropReq struct {
	poolRequest
	// ID identifies the pool for which this property should be set.
	ID         string
	Properties []*daos.PoolProperty
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

	rpcClient.Debugf("DAOS pool set-prop request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	return errors.Wrap(ur.getMSError(), "pool set-prop failed")
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
	Properties []*daos.PoolProperty
}

// PoolGetProp sends a pool get-prop request to the pool service leader.
func PoolGetProp(ctx context.Context, rpcClient UnaryInvoker, req *PoolGetPropReq) ([]*daos.PoolProperty, error) {
	// Get all by default.
	if len(req.Properties) == 0 {
		allProps := daos.PoolProperties()
		req.Properties = make([]*daos.PoolProperty, 0, len(allProps))
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

	rpcClient.Debugf("pool get-prop request: %s\n", pbUtil.Debug(pbReq))
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

	pbMap := make(map[uint32]*mgmtpb.PoolProperty)
	for _, prop := range pbResp.GetProperties() {
		if _, found := pbMap[prop.GetNumber()]; found {
			return nil, errors.Errorf("got > 1 %d in response", prop.GetNumber())
		}
		pbMap[prop.GetNumber()] = prop
	}

	resp := make([]*daos.PoolProperty, 0, len(req.Properties))
	for _, prop := range req.Properties {
		pbProp, found := pbMap[prop.Number]
		if !found {
			// Properties can be missing due to DAOS-11418 and DAOS-13919
			rpcClient.Debugf("can't find prop %d (%s) in resp", prop.Number, prop.Name)
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
		resp = append(resp, prop)
	}

	return resp, nil
}

// PoolRanksReq struct contains request for operation on multiple pool-ranks. Control API receives
// generic PoolRanksReq with multiple ranks from API consumer (e.g. dmg admin tool) and issues
// specific e.g. pb.PoolDrain gRPC requests to server for each rank in serial using the management
// service client API. Per-rank results are returned in control API generic PoolRanksResp.
type PoolRanksReq struct {
	poolRequest
	ID        string          `json:"id"`
	Ranks     []ranklist.Rank `json:"ranks"`
	TargetIdx []uint32        `json:"target_idx"`
	Force     bool            `json:"force"`
}

// PoolRankResult describes the result of an operation on a pool's rank. JSON compatible with
// shared RankResult protobuf message.
type PoolRankResult struct {
	Rank    ranklist.Rank `json:"rank"`
	Errored bool          `json:"errored"`
	Msg     string        `json:"msg"`
}

// PoolRanksResp struct contains response from operation on multiple pool-ranks.
type PoolRanksResp struct {
	ID      string            `json:"id"`
	Results []*PoolRankResult `json:"results"`
}

// Errors returns either a generic failure based on the requested rankset failure or a rank-specific
// failure depending on whether or not FailedRank has been set in the response.
func (resp *PoolRanksResp) Errors() error {
	if resp == nil {
		return errors.Errorf("nil %T", resp)
	}
	id := resp.ID
	if id == "" {
		id = "<unknown>"
	}

	rs := ranklist.MustCreateRankSet("")
	for _, res := range resp.Results {
		if res.Errored {
			rs.Add(res.Rank)
		}
	}

	if rs.Count() > 0 {
		return errors.Errorf("%s %s failed on pool %s",
			english.PluralWord(rs.Count(), "rank", "ranks"), rs.RangedString(), id)
	}

	return nil
}

type poolRankOpSig func(context.Context, UnaryInvoker, *PoolRanksReq, ranklist.Rank) (*PoolRankResult, error)

// Iterate through multiple ranks in PoolRanksReq and call supplied poolRankOp to generate and
// return a result.
func getPoolRanksResp(ctx context.Context, rpcClient UnaryInvoker, req *PoolRanksReq, poolRankOp poolRankOpSig) (*PoolRanksResp, error) {
	if req == nil {
		return nil, errors.Errorf("nil %T", req)
	}
	if req.ID == "" {
		return nil, errors.New("empty pool id")
	}
	if len(req.Ranks) == 0 {
		return nil, errors.New("no ranks in request")
	}

	results := []*PoolRankResult{}
	for _, rank := range req.Ranks {
		result, err := poolRankOp(ctx, rpcClient, req, rank)
		if err != nil {
			rpcClient.Debugf("pool %s rank %d failed: %s", req.ID, rank, err.Error())
			result = &PoolRankResult{
				Rank:    rank,
				Errored: true,
				Msg:     err.Error(),
			}
		}
		results = append(results, result)
	}

	return &PoolRanksResp{
		ID:      req.ID,
		Results: results,
	}, nil
}

func getPoolRankResult(status int32, rank ranklist.Rank) *PoolRankResult {
	var msg string
	errored := daos.Status(status) != daos.Success
	if errored {
		msg = daos.Status(status).Error()
	}

	return &PoolRankResult{Rank: rank, Errored: errored, Msg: msg}
}

// Implements poolRankOpSig.
func poolExcludeRank(ctx context.Context, rpcClient UnaryInvoker, req *PoolRanksReq, rank ranklist.Rank) (*PoolRankResult, error) {
	pbReq := new(mgmtpb.PoolExcludeReq)
	if err := convert.Types(req, pbReq); err != nil {
		return nil, errors.Wrapf(err, "convert %T->%T", req, pbReq)
	}
	pbReq.Sys = req.getSystem(rpcClient)
	pbReq.Rank = rank.Uint32()

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolExclude(ctx, pbReq)
	})

	rpcClient.Debugf("Exclude DAOS pool-rank targets request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.PoolExcludeResp)
	if err := convertMSResponse(ur, resp); err != nil {
		return nil, err
	}
	rpcClient.Debugf("Exclude DAOS pool-rank targets response: %+v\n", *resp)

	return getPoolRankResult(resp.Status, rank), nil
}

// PoolExclude will set pool targets on specified ranks to down state which should automatically
// start the rebuilding process. Returns pool-ranks response and error.
func PoolExclude(ctx context.Context, rpcClient UnaryInvoker, req *PoolRanksReq) (*PoolRanksResp, error) {
	return getPoolRanksResp(ctx, rpcClient, req, poolExcludeRank)
}

// Implements poolRankOpSig.
func poolDrainRank(ctx context.Context, rpcClient UnaryInvoker, req *PoolRanksReq, rank ranklist.Rank) (*PoolRankResult, error) {
	pbReq := new(mgmtpb.PoolDrainReq)
	if err := convert.Types(req, pbReq); err != nil {
		return nil, errors.Wrapf(err, "convert %T->%T", req, pbReq)
	}
	pbReq.Sys = req.getSystem(rpcClient)
	pbReq.Rank = rank.Uint32()

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolDrain(ctx, pbReq)
	})

	rpcClient.Debugf("Drain DAOS pool-rank targets request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.PoolDrainResp)
	if err := convertMSResponse(ur, resp); err != nil {
		return nil, err
	}
	rpcClient.Debugf("Drain DAOS pool-rank targets response: %+v\n", *resp)

	return getPoolRankResult(resp.Status, rank), nil
}

// PoolDrain will set pool targets on specified ranks in to the drain state which should
// automatically start the rebuildiing process. Returns pool-ranks response and error.
// Drain engages a cooperative transfer of data before rank gets excluded.
func PoolDrain(ctx context.Context, rpcClient UnaryInvoker, req *PoolRanksReq) (*PoolRanksResp, error) {
	return getPoolRanksResp(ctx, rpcClient, req, poolDrainRank)
}

// PoolExtendReq struct contains request
type PoolExtendReq struct {
	poolRequest
	ID    string
	Ranks []ranklist.Rank
}

// PoolExtend will extend the DAOS pool by the specified ranks.
// This should automatically start the rebalance process.
// Returns an error (including any DER code from DAOS).
func PoolExtend(ctx context.Context, rpcClient UnaryInvoker, req *PoolExtendReq) error {
	pbReq := new(mgmtpb.PoolExtendReq)
	if err := convert.Types(req, pbReq); err != nil {
		return errors.Wrapf(err, "convert %T->%T", req, pbReq)
	}
	pbReq.Sys = req.getSystem(rpcClient)

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolExtend(ctx, pbReq)
	})

	rpcClient.Debugf("Extend DAOS pool request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	return errors.Wrap(ur.getMSError(), "pool extend failed")
}

// Implements poolRankOpSig.
func poolReintegrateRank(ctx context.Context, rpcClient UnaryInvoker, req *PoolRanksReq, rank ranklist.Rank) (*PoolRankResult, error) {
	pbReq := new(mgmtpb.PoolReintReq)
	if err := convert.Types(req, pbReq); err != nil {
		return nil, errors.Wrapf(err, "convert %T->%T", req, pbReq)
	}
	pbReq.Sys = req.getSystem(rpcClient)
	pbReq.Rank = rank.Uint32()

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolReintegrate(ctx, pbReq)
	})

	rpcClient.Debugf("Reintegrate DAOS pool-rank targets request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(mgmtpb.PoolReintResp)
	if err := convertMSResponse(ur, resp); err != nil {
		return nil, err
	}
	rpcClient.Debugf("Reintegrate DAOS pool-rank targets response: %+v\n", *resp)

	return getPoolRankResult(resp.Status, rank), nil
}

// PoolReintegrate will set pool targets on specified ranks back to up state which should
// automatically start the reintegration process. Returns pool-ranks response and error.
func PoolReintegrate(ctx context.Context, rpcClient UnaryInvoker, req *PoolRanksReq) (*PoolRanksResp, error) {
	return getPoolRanksResp(ctx, rpcClient, req, poolReintegrateRank)
}

// ListPoolsReq contains the inputs for the list pools command.
type ListPoolsReq struct {
	unaryRequest
	msRequest
	NoQuery bool
}

// PoolQueryErr contains the error if a pool query failed.
type PoolQueryErr struct {
	Error  error
	Status daos.Status
}

// ListPoolsResp contains the status of the request and, if successful, the list
// of pools in the system.
type ListPoolsResp struct {
	Status      int32                       `json:"status"`
	Pools       []*daos.PoolInfo            `json:"pools"`
	QueryErrors map[uuid.UUID]*PoolQueryErr `json:"-"` // NB: Exported because of tests in other packages.
}

// PoolQueryError returns the error if PoolQuery failed for the given pool.
func (lpr *ListPoolsResp) PoolQueryError(poolUUID uuid.UUID) error {
	qe, found := lpr.QueryErrors[poolUUID]
	if !found {
		return nil
	}

	if qe.Status != daos.Success {
		return qe.Status
	}

	return qe.Error
}

// Validate returns error if response contents are unexpected, string of
// warnings if pool queries have failed or nil values if contents are expected.
func (lpr *ListPoolsResp) Validate() (string, error) {
	var numTiers int
	out := new(strings.Builder)

	for i, p := range lpr.Pools {
		if p.UUID == uuid.Nil {
			return "", errors.Errorf("pool with index %d has no uuid", i)
		}
		if qe, found := lpr.QueryErrors[p.UUID]; found {
			if qe.Error != nil {
				fmt.Fprintf(out, "Query on pool %q unsuccessful, error: %q\n",
					p.Name(), qe.Error)
				continue // no usage stats expected
			}
			if qe.Status != daos.Success {
				fmt.Fprintf(out, "Query on pool %q unsuccessful, status: %q\n",
					p.Name(), qe.Status)
				continue // no usage stats expected
			}
		}

		poolUsage := p.Usage()
		if len(poolUsage) == 0 {
			continue // no usage stats in response
		}
		if numTiers != 0 && len(poolUsage) != numTiers {
			return "", errors.Errorf("pool %s has %d storage tiers, want %d",
				p.UUID, len(poolUsage), numTiers)
		}
		numTiers = len(poolUsage)
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

func newListPoolsResp() *ListPoolsResp {
	return &ListPoolsResp{
		QueryErrors: make(map[uuid.UUID]*PoolQueryErr),
	}
}

// ListPools fetches the list of all pools and their service replicas from the
// system.
func ListPools(ctx context.Context, rpcClient UnaryInvoker, req *ListPoolsReq) (*ListPoolsResp, error) {
	pbReq := &mgmtpb.ListPoolsReq{Sys: req.getSystem(rpcClient)}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).ListPools(ctx, pbReq)
	})

	rpcClient.Debugf("DAOS system list-pools request: %s", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := newListPoolsResp()
	if err := convertMSResponse(ur, resp); err != nil {
		return nil, err
	}

	if req.NoQuery {
		return resp, nil
	}

	// issue query request and populate usage statistics for each pool
	for i, p := range resp.Pools {
		if p.State != daos.PoolServiceStateReady {
			rpcClient.Debugf("Skipping query of pool in state: %s", p.State)
			continue
		}
		rpcClient.Debugf("Fetching details for discovered pool: %v", p)

		pqr := &PoolQueryResp{PoolInfo: *p}
		_, err := poolQueryInt(ctx, rpcClient, &PoolQueryReq{ID: p.UUID.String()}, pqr)
		if err != nil {
			resp.QueryErrors[p.UUID] = &PoolQueryErr{Error: err}
			continue
		}
		if pqr.Status != 0 {
			resp.QueryErrors[p.UUID] = &PoolQueryErr{Status: daos.Status(pqr.Status)}
			continue
		}
		if p.UUID != pqr.UUID {
			return nil, errors.New("pool query response uuid does not match request")

		}
		resp.Pools[i] = &pqr.PoolInfo
	}

	sort.Slice(resp.Pools, func(i int, j int) bool {
		l, r := resp.Pools[i], resp.Pools[j]
		if l == nil || r == nil {
			return false
		}
		return l.Label < r.Label
	})

	for _, p := range resp.Pools {
		rpcClient.Debugf("DAOS system pool in list-pools response: %+v", p)
	}

	return resp, nil
}

type rankFreeSpaceMap map[ranklist.Rank]uint64

type filterRankFn func(rank ranklist.Rank) bool

func newFilterRankFunc(ranks ranklist.RankList) filterRankFn {
	return func(rank ranklist.Rank) bool {
		return len(ranks) == 0 || rank.InList(ranks)
	}
}

// Add namespace ranks to rankNVMeFreeSpace map and return minimum free available SCM namespace bytes.
func processSCMSpaceStats(log debugLogger, filterRank filterRankFn, scmNamespaces storage.ScmNamespaces, rankNVMeFreeSpace rankFreeSpaceMap) (uint64, error) {
	scmBytes := uint64(math.MaxUint64)

	// Realistically there should only be one-per-rank but handle the case for multiple anyway.
	for _, scmNamespace := range scmNamespaces {
		if scmNamespace.Mount == nil {
			return 0, errors.Errorf("SCM device %s (bdev %s, name %s) is not mounted",
				scmNamespace.UUID, scmNamespace.BlockDevice, scmNamespace.Name)
		}

		if !filterRank(scmNamespace.Mount.Rank) {
			log.Debugf("Skipping SCM device %s (bdev %s, name %s, rank %d) not in ranklist",
				scmNamespace.UUID, scmNamespace.BlockDevice, scmNamespace.Name,
				scmNamespace.Mount.Rank)
			continue
		}

		usableBytes := scmNamespace.Mount.UsableBytes

		if scmBytes > usableBytes {
			scmBytes = usableBytes
		}

		if _, exists := rankNVMeFreeSpace[scmNamespace.Mount.Rank]; exists {
			return 0, errors.Errorf("Multiple SCM devices found for rank %d",
				scmNamespace.Mount.Rank)
		}

		// Initialize entry for rank in NVMe free space map.
		rankNVMeFreeSpace[scmNamespace.Mount.Rank] = 0
	}

	return scmBytes, nil
}

// Add NVMe free bytes to rankNVMeFreeSpace map.
func processNVMeSpaceStats(log debugLogger, filterRank filterRankFn, nvmeControllers storage.NvmeControllers, rankNVMeFreeSpace rankFreeSpaceMap) error {
	for _, controller := range nvmeControllers {
		for _, smdDevice := range controller.SmdDevices {
			msgDev := fmt.Sprintf("SMD device %s (rank %d, ctrlr %s", smdDevice.UUID,
				smdDevice.Rank, controller.PciAddr)

			if smdDevice.Roles.IsEmpty() {
				msgDev += ")"
			} else {
				msgDev += fmt.Sprintf(", roles %q)", smdDevice.Roles.String())
				if !smdDevice.Roles.HasData() {
					log.Debugf("skipping %s, not used for storing data", msgDev)
					continue
				}
			}

			if controller.NvmeState == storage.NvmeStateNew {
				log.Debugf("Skipping %s, not used as in NEW state", msgDev)
				continue
			}
			if controller.NvmeState != storage.NvmeStateNormal {
				return errors.Errorf("%s not usable as in %s state", msgDev,
					controller.NvmeState.String())
			}

			if !filterRank(smdDevice.Rank) {
				log.Debugf("Skipping %s, not in ranklist", msgDev)
				continue
			}

			if _, exists := rankNVMeFreeSpace[smdDevice.Rank]; !exists {
				return errors.Errorf("Rank %d without SCM device and at least one %s",
					smdDevice.Rank, msgDev)
			}

			rankNVMeFreeSpace[smdDevice.Rank] += smdDevice.UsableBytes

			log.Debugf("Added %s as usable: device state=%q, smd-size=%d ctrlr-total-free=%d",
				msgDev, controller.NvmeState.String(), smdDevice.UsableBytes,
				rankNVMeFreeSpace[smdDevice.Rank])
		}
	}

	return nil
}

// Return the maximal SCM and NVMe size of a pool which could be created with all the storage nodes.
func getMaxPoolSize(ctx context.Context, rpcClient UnaryInvoker, createReq *PoolCreateReq) (uint64, uint64, error) {
	if createReq.MemRatio < 0 {
		return 0, 0, errors.New("invalid mem-ratio, should be greater than zero")
	}
	if createReq.MemRatio > 1 {
		return 0, 0, errors.New("invalid mem-ratio, should not be greater than one")
	}

	// Verify that the DAOS system is ready before attempting to query storage.
	if _, err := SystemQuery(ctx, rpcClient, &SystemQueryReq{}); err != nil {
		return 0, 0, err
	}

	scanReq := &StorageScanReq{
		Usage:    true,
		MemRatio: createReq.MemRatio,
	}

	scanResp, err := StorageScan(ctx, rpcClient, scanReq)
	if err != nil {
		return 0, 0, err
	}

	if len(scanResp.HostStorage) == 0 {
		return 0, 0, errors.New("Empty host storage response from StorageScan")
	}

	// Generate function to verify a rank is in the provided rank slice.
	filterRank := newFilterRankFunc(ranklist.RankList(createReq.Ranks))
	rankNVMeFreeSpace := make(rankFreeSpaceMap)
	scmBytes := uint64(math.MaxUint64)
	for _, key := range scanResp.HostStorage.Keys() {
		hostStorage := scanResp.HostStorage[key].HostStorage

		if hostStorage.ScmNamespaces.Usable() == 0 {
			return 0, 0, errors.Errorf("Host without SCM storage: hostname=%s",
				scanResp.HostStorage[key].HostSet.String())
		}

		sb, err := processSCMSpaceStats(rpcClient, filterRank, hostStorage.ScmNamespaces, rankNVMeFreeSpace)
		if err != nil {
			return 0, 0, err
		}

		if scmBytes > sb {
			scmBytes = sb
		}

		if err := processNVMeSpaceStats(rpcClient, filterRank, hostStorage.NvmeDevices, rankNVMeFreeSpace); err != nil {
			return 0, 0, err
		}
	}

	if scmBytes == math.MaxUint64 {
		return 0, 0, errors.Errorf("No SCM storage space available with rank list %q",
			createReq.Ranks)
	}

	nvmeBytes := uint64(math.MaxUint64)
	for _, nvmeRankBytes := range rankNVMeFreeSpace {
		if nvmeBytes > nvmeRankBytes {
			nvmeBytes = nvmeRankBytes
		}
	}

	if !scanResp.HostStorage.IsMdOnSsdEnabled() {
		rpcClient.Debugf("Maximal size of a pool: scmBytes=%s (%d B) nvmeBytes=%s (%d B)",
			humanize.Bytes(scmBytes), scmBytes, humanize.Bytes(nvmeBytes), nvmeBytes)

		return scmBytes, nvmeBytes, nil
	}
	rpcClient.Debugf("md-on-ssd mode detected")

	// In MD-on-SSD mode calculate metaBytes based on the minimum ramdisk (called scm here)
	// availability across ranks. NVMe sizes returned in StorageScan response at the beginning
	// of this function have been adjusted based on SSD bdev roles and MemRatio passed in the
	// scan request. The rationale behind deriving pool sizes from ramdisk availability is that
	// this is more likely to be the limiting factor than SSD usage.
	if createReq.MemRatio == 0 {
		createReq.MemRatio = 1
	}
	metaBytes := uint64(float64(scmBytes) / float64(createReq.MemRatio))

	rpcClient.Debugf("With minimum available ramdisk capacity of %s and mem-ratio %.2f,"+
		" the maximum per-rank sizes for a pool are META=%s (%d B) and DATA=%s (%d B)",
		humanize.Bytes(scmBytes), createReq.MemRatio, humanize.Bytes(metaBytes),
		metaBytes, humanize.Bytes(nvmeBytes), nvmeBytes)

	return metaBytes, nvmeBytes, nil
}
