//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"encoding/json"
	"fmt"
	"math"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/dustin/go-humanize"
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
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security/auth"
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
		Properties []*mgmtpb.PoolProperty `json:"properties"`
		ACL        []string               `json:"acl"`
		*toJSON
	}{
		Properties: props,
		ACL:        acl,
		toJSON:     (*toJSON)(pcr),
	})
}

// request, filling in any missing fields with reasonable defaults.
func genPoolCreateRequest(in *PoolCreateReq) (out *mgmtpb.PoolCreateReq, err error) {
	if in.userExt == nil {
		in.userExt = &auth.External{}
	}
	// ensure pool ownership is set up correctly
	in.User, in.UserGroup, err = formatNameGroup(in.userExt, in.User, in.UserGroup)
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
		userExt    auth.UserExt
		User       string
		UserGroup  string
		ACL        *AccessControlList `json:"-"`
		NumSvcReps uint32
		Properties []*daos.PoolProperty `json:"-"`
		// auto-config params
		TotalBytes uint64
		TierRatio  []float64
		NumRanks   uint32
		// manual params
		Ranks     []ranklist.Rank
		TierBytes []uint64
	}

	// PoolCreateResp contains the response from a pool create request.
	PoolCreateResp struct {
		UUID      string   `json:"uuid"`
		Leader    uint32   `json:"svc_ldr"`
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
		ID                   string
		IncludeEnabledRanks  bool
		IncludeDisabledRanks bool
	}

	// StorageUsageStats represents DAOS storage usage statistics.
	StorageUsageStats struct {
		Total     uint64           `json:"total"`
		Free      uint64           `json:"free"`
		Min       uint64           `json:"min"`
		Max       uint64           `json:"max"`
		Mean      uint64           `json:"mean"`
		MediaType StorageMediaType `json:"media_type"`
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
		TotalTargets     uint32               `json:"total_targets"`
		ActiveTargets    uint32               `json:"active_targets"`
		TotalEngines     uint32               `json:"total_engines"`
		DisabledTargets  uint32               `json:"disabled_targets"`
		Version          uint32               `json:"version"`
		Leader           uint32               `json:"leader"`
		Rebuild          *PoolRebuildStatus   `json:"rebuild"`
		TierStats        []*StorageUsageStats `json:"tier_stats"`
		EnabledRanks     *ranklist.RankSet    `json:"-"`
		DisabledRanks    *ranklist.RankSet    `json:"-"`
		PoolLayoutVer    uint32               `json:"pool_layout_ver"`
		UpgradeLayoutVer uint32               `json:"upgrade_layout_ver"`
	}

	// PoolQueryResp contains the pool query response.
	PoolQueryResp struct {
		Status int32  `json:"status"`
		UUID   string `json:"uuid"`
		PoolInfo
	}

	// PoolQueryTargetReq contains parameters for a pool query target request
	PoolQueryTargetReq struct {
		poolRequest
		ID      string
		Rank    ranklist.Rank
		Targets []uint32
	}

	StorageMediaType int32

	// StorageTargetUsage represents DAOS target storage usage
	StorageTargetUsage struct {
		Total     uint64           `json:"total"`
		Free      uint64           `json:"free"`
		MediaType StorageMediaType `json:"media_type"`
	}

	PoolQueryTargetType  int32
	PoolQueryTargetState int32

	// PoolQueryTargetInfo contains information about a single target
	PoolQueryTargetInfo struct {
		Type  PoolQueryTargetType  `json:"target_type"`
		State PoolQueryTargetState `json:"target_state"`
		Space []*StorageTargetUsage
	}

	// PoolQueryTargetResp contains a pool query target response
	PoolQueryTargetResp struct {
		Status int32 `json:"status"`
		Infos  []*PoolQueryTargetInfo
	}
)

const (
	// StorageMediaTypeScm indicates that the media is storage class (persistent) memory
	StorageMediaTypeScm StorageMediaType = iota
	// StorageMediaTypeNvme indicates that the media is NVMe SSD
	StorageMediaTypeNvme
)

func (pqr *PoolQueryResp) MarshalJSON() ([]byte, error) {
	if pqr == nil {
		return []byte("null"), nil
	}

	type Alias PoolQueryResp
	aux := &struct {
		EnabledRanks  *[]ranklist.Rank `json:"enabled_ranks"`
		DisabledRanks *[]ranklist.Rank `json:"disabled_ranks"`
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

func unmarshallRankSet(ranks string) (*ranklist.RankSet, error) {
	switch ranks {
	case "":
		return nil, nil
	case "[]":
		return &ranklist.RankSet{}, nil
	default:
		return ranklist.CreateRankSet(ranks)
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

const (
	// PoolRebuildStateIdle indicates that the rebuild process is idle.
	PoolRebuildStateIdle PoolRebuildState = iota
	// PoolRebuildStateDone indicates that the rebuild process has completed.
	PoolRebuildStateDone
	// PoolRebuildStateBusy indicates that the rebuild process is in progress.
	PoolRebuildStateBusy
)

func (prs PoolRebuildState) String() string {
	prss, ok := mgmtpb.PoolRebuildStatus_State_name[int32(prs)]
	if !ok {
		return "unknown"
	}
	return strings.ToLower(prss)
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
	pbReq := &mgmtpb.PoolQueryReq{
		Sys:                  req.getSystem(rpcClient),
		Id:                   req.ID,
		IncludeEnabledRanks:  req.IncludeEnabledRanks,
		IncludeDisabledRanks: req.IncludeDisabledRanks,
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolQuery(ctx, pbReq)
	})

	rpcClient.Debugf("Query DAOS pool request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	pqr := new(PoolQueryResp)
	return pqr, convertMSResponse(ur, pqr)
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

func (smt StorageMediaType) MarshalJSON() ([]byte, error) {
	typeStr, ok := mgmtpb.StorageMediaType_name[int32(smt)]
	if !ok {
		return nil, errors.Errorf("invalid storage media type %d", smt)
	}
	return []byte(`"` + strings.ToLower(typeStr) + `"`), nil
}

func (smt StorageMediaType) String() string {
	smts, ok := mgmtpb.StorageMediaType_name[int32(smt)]
	if !ok {
		return "unknown"
	}
	return strings.ToLower(smts)
}

func (pqtt PoolQueryTargetType) MarshalJSON() ([]byte, error) {
	typeStr, ok := mgmtpb.PoolQueryTargetInfo_TargetType_name[int32(pqtt)]
	if !ok {
		return nil, errors.Errorf("invalid target type %d", pqtt)
	}
	return []byte(`"` + strings.ToLower(typeStr) + `"`), nil
}

func (ptt PoolQueryTargetType) String() string {
	ptts, ok := mgmtpb.PoolQueryTargetInfo_TargetType_name[int32(ptt)]
	if !ok {
		return "invalid"
	}
	return strings.ToLower(ptts)
}

const (
	PoolTargetStateUnknown PoolQueryTargetState = iota
	// PoolTargetStateDownOut indicates the target is not available
	PoolTargetStateDownOut
	// PoolTargetStateDown indicates the target is not available, may need rebuild
	PoolTargetStateDown
	// PoolTargetStateUp indicates the target is up
	PoolTargetStateUp
	// PoolTargetStateUpIn indicates the target is up and running
	PoolTargetStateUpIn
	// PoolTargetStateNew indicates the target is in an intermediate state (pool map change)
	PoolTargetStateNew
	// PoolTargetStateDrain indicates the target is being drained
	PoolTargetStateDrain
)

func (pqts PoolQueryTargetState) MarshalJSON() ([]byte, error) {
	stateStr, ok := mgmtpb.PoolQueryTargetInfo_TargetState_name[int32(pqts)]
	if !ok {
		return nil, errors.Errorf("invalid target state %d", pqts)
	}
	return []byte(`"` + strings.ToLower(stateStr) + `"`), nil
}

func (pts PoolQueryTargetState) String() string {
	ptss, ok := mgmtpb.PoolQueryTargetInfo_TargetState_name[int32(pts)]
	if !ok {
		return "invalid"
	}
	return strings.ToLower(ptss)
}

// For using the pretty printer that dmg uses for this target info.
func convertPoolTargetInfo(pbInfo *mgmtpb.PoolQueryTargetInfo) (*PoolQueryTargetInfo, error) {
	pqti := new(PoolQueryTargetInfo)
	pqti.Type = PoolQueryTargetType(pbInfo.Type)
	pqti.State = PoolQueryTargetState(pbInfo.State)
	pqti.Space = []*StorageTargetUsage{
		{
			Total:     uint64(pbInfo.Space[StorageMediaTypeScm].Total),
			Free:      uint64(pbInfo.Space[StorageMediaTypeScm].Free),
			MediaType: StorageMediaTypeScm,
		},
		{
			Total:     uint64(pbInfo.Space[StorageMediaTypeNvme].Total),
			Free:      uint64(pbInfo.Space[StorageMediaTypeNvme].Free),
			MediaType: StorageMediaTypeNvme,
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

// PoolExcludeReq struct contains request
type PoolExcludeReq struct {
	poolRequest
	ID        string
	Rank      ranklist.Rank
	Targetidx []uint32
}

// ExcludeResp has no other parameters other than success/failure for now.

// PoolExclude will set a pool target for a specific rank to down.
// This should automatically start the rebuildiing process.
// Returns an error (including any DER code from DAOS).
func PoolExclude(ctx context.Context, rpcClient UnaryInvoker, req *PoolExcludeReq) error {
	pbReq := &mgmtpb.PoolExcludeReq{
		Sys:       req.getSystem(rpcClient),
		Id:        req.ID,
		Rank:      req.Rank.Uint32(),
		Targetidx: req.Targetidx,
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolExclude(ctx, pbReq)
	})

	rpcClient.Debugf("Exclude DAOS pool target request: %s\n", pbReq)
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	return errors.Wrap(ur.getMSError(), "pool exclude failed")
}

// PoolDrainReq struct contains request
type PoolDrainReq struct {
	poolRequest
	ID        string
	Rank      ranklist.Rank
	Targetidx []uint32
}

// DrainResp has no other parameters other than success/failure for now.

// PoolDrain will set a pool target for a specific rank to the drain status.
// This should automatically start the rebuildiing process.
// Returns an error (including any DER code from DAOS).
func PoolDrain(ctx context.Context, rpcClient UnaryInvoker, req *PoolDrainReq) error {
	pbReq := &mgmtpb.PoolDrainReq{
		Sys:       req.getSystem(rpcClient),
		Id:        req.ID,
		Rank:      req.Rank.Uint32(),
		Targetidx: req.Targetidx,
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolDrain(ctx, pbReq)
	})

	rpcClient.Debugf("Drain DAOS pool target request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	return errors.Wrap(ur.getMSError(), "pool drain failed")
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
	Ranks []ranklist.Rank
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

	rpcClient.Debugf("Extend DAOS pool request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	return errors.Wrap(ur.getMSError(), "pool extend failed")
}

// PoolReintegrateReq struct contains request
type PoolReintegrateReq struct {
	poolRequest
	ID        string
	Rank      ranklist.Rank
	Targetidx []uint32
}

// ReintegrateResp has no other parameters other than success/failure for now.

// PoolReintegrate will set a pool target for a specific rank back to up.
// This should automatically start the reintegration process.
// Returns an error (including any DER code from DAOS).
func PoolReintegrate(ctx context.Context, rpcClient UnaryInvoker, req *PoolReintegrateReq) error {
	pbReq := &mgmtpb.PoolReintegrateReq{
		Sys:       req.getSystem(rpcClient),
		Id:        req.ID,
		Rank:      req.Rank.Uint32(),
		Targetidx: req.Targetidx,
	}

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).PoolReintegrate(ctx, pbReq)
	})

	rpcClient.Debugf("Reintegrate DAOS pool target request: %s\n", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	return errors.Wrap(ur.getMSError(), "pool reintegrate failed")
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
		// ServiceLeader is the current pool service leader.
		ServiceLeader uint32 `json:"svc_ldr"`
		// ServiceReplicas is the list of ranks on which this pool's
		// service replicas are running.
		ServiceReplicas []ranklist.Rank `json:"svc_reps"`
		// State is the current state of the pool.
		State string `json:"state"`

		// TargetsTotal is the total number of targets in pool.
		TargetsTotal uint32 `json:"targets_total"`
		// TargetsDisabled is the number of inactive targets in pool.
		TargetsDisabled uint32 `json:"targets_disabled"`

		// UpgradeLayoutVer is latest pool layout version to be upgraded.
		UpgradeLayoutVer uint32 `json:"upgrade_layout_ver"`
		// PoolLayoutVer is current pool layout version.
		PoolLayoutVer uint32 `json:"pool_layout_ver"`

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
	pbReq := &mgmtpb.ListPoolsReq{Sys: req.getSystem(rpcClient)}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).ListPools(ctx, pbReq)
	})

	rpcClient.Debugf("DAOS system list-pools request: %s", pbUtil.Debug(pbReq))
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
			p.QueryStatusMsg = daos.Status(resp.Status).Error()
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
		p.PoolLayoutVer = resp.PoolLayoutVer
		p.UpgradeLayoutVer = resp.UpgradeLayoutVer
		p.setUsage(resp)
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
func processSCMSpaceStats(log logging.Logger, filterRank filterRankFn, scmNamespaces storage.ScmNamespaces, rankNVMeFreeSpace rankFreeSpaceMap) (uint64, error) {
	scmBytes := uint64(math.MaxUint64)

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
func processNVMeSpaceStats(log logging.Logger, filterRank filterRankFn, nvmeControllers storage.NvmeControllers, rankNVMeFreeSpace rankFreeSpaceMap) error {
	for _, nvmeController := range nvmeControllers {
		for _, smdDevice := range nvmeController.SmdDevices {
			if !smdDevice.Roles.IsEmpty() && (smdDevice.Roles.OptionBits&storage.BdevRoleData) == 0 {
				log.Debugf("Skipping SMD device %s (rank %d, ctrlr %s) not used for storing data",
					smdDevice.UUID, smdDevice.Rank, smdDevice.TrAddr, smdDevice.Rank)
				continue
			}

			if smdDevice.NvmeState != storage.NvmeStateNormal {
				log.Noticef("SMD device %s (rank %d, ctrlr %s) not usable (device state %q)",
					smdDevice.UUID, smdDevice.Rank, smdDevice.TrAddr, smdDevice.NvmeState.String())
				continue
			}

			if !filterRank(smdDevice.Rank) {
				log.Debugf("Skipping SMD device %s (rank %d, ctrlr %s) not in ranklist",
					smdDevice.UUID, smdDevice.Rank, smdDevice.TrAddr, smdDevice.Rank)
				continue
			}

			if _, exists := rankNVMeFreeSpace[smdDevice.Rank]; !exists {
				return errors.Errorf("Rank %d without SCM device and at least one SMD device %s (rank %d, ctrlr %s)",
					smdDevice.Rank, smdDevice.UUID, smdDevice.Rank, smdDevice.TrAddr)
			}

			rankNVMeFreeSpace[smdDevice.Rank] += smdDevice.UsableBytes

			log.Debugf("Added SMD device %s (rank %d, ctrlr %s) is usable: device state=%q, smd-size=%d ctrlr-total-free=%d",
				smdDevice.UUID, smdDevice.Rank, smdDevice.TrAddr, smdDevice.NvmeState.String(),
				smdDevice.UsableBytes, rankNVMeFreeSpace[smdDevice.Rank])
		}
	}

	return nil
}

// Return the maximal SCM and NVMe size of a pool which could be created with all the storage nodes.
func GetMaxPoolSize(ctx context.Context, log logging.Logger, rpcClient UnaryInvoker, ranks ranklist.RankList) (uint64, uint64, error) {
	// Verify that the DAOS system is ready before attempting to query storage.
	if _, err := SystemQuery(ctx, rpcClient, &SystemQueryReq{}); err != nil {
		return 0, 0, err
	}

	resp, err := StorageScan(ctx, rpcClient, &StorageScanReq{Usage: true})
	if err != nil {
		return 0, 0, err
	}

	if len(resp.HostStorage) == 0 {
		return 0, 0, errors.New("Empty host storage response from StorageScan")
	}

	// Generate function to verify a rank is in the provided rank slice.
	filterRank := newFilterRankFunc(ranks)
	rankNVMeFreeSpace := make(rankFreeSpaceMap)
	scmBytes := uint64(math.MaxUint64)
	for _, key := range resp.HostStorage.Keys() {
		hostStorage := resp.HostStorage[key].HostStorage

		if hostStorage.ScmNamespaces.Usable() == 0 {
			return 0, 0, errors.Errorf("Host without SCM storage: hostname=%s",
				resp.HostStorage[key].HostSet.String())
		}

		sb, err := processSCMSpaceStats(log, filterRank, hostStorage.ScmNamespaces, rankNVMeFreeSpace)
		if err != nil {
			return 0, 0, err
		}

		if scmBytes > sb {
			scmBytes = sb
		}

		if err := processNVMeSpaceStats(log, filterRank, hostStorage.NvmeDevices, rankNVMeFreeSpace); err != nil {
			return 0, 0, err
		}
	}

	if scmBytes == math.MaxUint64 {
		return 0, 0, errors.Errorf("No SCM storage space available with rank list %s", ranks)
	}

	nvmeBytes := uint64(math.MaxUint64)
	for _, nvmeRankBytes := range rankNVMeFreeSpace {
		if nvmeBytes > nvmeRankBytes {
			nvmeBytes = nvmeRankBytes
		}
	}

	rpcClient.Debugf("Maximal size of a pool: scmBytes=%s (%d B) nvmeBytes=%s (%d B)",
		humanize.Bytes(scmBytes), scmBytes, humanize.Bytes(nvmeBytes), nvmeBytes)

	return scmBytes, nvmeBytes, nil
}
