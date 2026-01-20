//
// (C) Copyright 2022-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package proto

import (
	"fmt"
	"strings"

	"github.com/dustin/go-humanize"
	"google.golang.org/protobuf/proto"

	grpcpb "github.com/Jille/raft-grpc-transport/proto"
	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

// ShouldDebug returns true if the protobuf message should be logged.
func ShouldDebug(msg proto.Message, ldrChk func() bool) bool {
	switch msg.(type) {
	case *grpcpb.AppendEntriesRequest, *grpcpb.AppendEntriesResponse,
		*grpcpb.RequestVoteRequest, *grpcpb.RequestVoteResponse,
		*grpcpb.TimeoutNowRequest, *grpcpb.TimeoutNowResponse,
		*grpcpb.InstallSnapshotRequest, *grpcpb.InstallSnapshotResponse,
		*mgmtpb.LeaderQueryReq, *mgmtpb.SystemQueryReq,
		*ctlpb.StorageScanResp, *ctlpb.NetworkScanResp,
		*ctlpb.StorageFormatResp, *ctlpb.PrepareScmResp,
		*mgmtpb.GetAttachInfoReq:
		return false
	default:
		// Most mgmt messages must be processed by the leader, so if this node
		// is not the leader, don't log them.
		if !ldrChk() && strings.HasPrefix(string(proto.MessageName(msg)), "mgmt.") {
			switch msg.(type) {
			case *mgmtpb.PoolQueryReq, *mgmtpb.PoolGetPropReq,
				*mgmtpb.ListPoolsReq, *mgmtpb.GetACLReq,
				*mgmtpb.PoolQueryTargetReq, *mgmtpb.ListContReq,
				*mgmtpb.SystemEraseReq, *mgmtpb.SystemGetPropReq,
				*mgmtpb.SystemGetAttrReq:
				return true
			default:
				return false
			}
		}
		return true
	}
}

// Debug returns a string representation of the protobuf message.
// Certain messages have hand-crafted representations for optimal
// log density and relevance. The default representation should
// be sufficient for most messages.
//
// NB: This is fairly expensive and should only be invoked when debug
// logging is enabled.
func Debug(msg proto.Message) string {
	if common.InterfaceIsNil(msg) {
		return fmt.Sprintf("nil %T", msg)
	}

	var bld strings.Builder
	switch m := msg.(type) {
	case *mgmtpb.SystemQueryResp:
		stateRanks := make(map[string]*ranklist.RankSet)
		for _, m := range m.Members {
			if _, found := stateRanks[m.State]; !found {
				stateRanks[m.State] = &ranklist.RankSet{}
			}
			stateRanks[m.State].Add(ranklist.Rank(m.Rank))
		}
		fmt.Fprintf(&bld, "%T@%d ", m, m.DataVersion)
		for state, set := range stateRanks {
			fmt.Fprintf(&bld, "%s:%s ", state, set.String())
		}
	case *mgmtpb.PoolCreateReq:
		fmt.Fprintf(&bld, "%T uuid:%s u:%s g:%s ", m, m.Uuid, m.User, m.UserGroup)
		if len(m.Properties) > 0 {
			fmt.Fprintf(&bld, "p:%+v ", m.Properties)
		}
		ranks := &ranklist.RankSet{}
		for _, r := range m.Ranks {
			ranks.Add(ranklist.Rank(r))
		}
		fmt.Fprintf(&bld, "ranks:%s ", ranks.String())
		fmt.Fprint(&bld, "tiers:")
		for i, b := range m.TierBytes {
			fmt.Fprintf(&bld, "%d: %s (%d)", i, humanize.Bytes(b), b)
			if len(m.TierRatio) > i+1 {
				fmt.Fprintf(&bld, "(%.02f%%) ", m.TierRatio[i])
			}
		}
		fmt.Fprintf(&bld, "mem-ratio: %.02f ", m.MemRatio)
	case *mgmtpb.PoolCreateResp:
		fmt.Fprintf(&bld, "%T svc_ldr:%d ", m, m.SvcLdr)
		ranks := &ranklist.RankSet{}
		for _, r := range m.SvcReps {
			ranks.Add(ranklist.Rank(r))
		}
		fmt.Fprintf(&bld, "svc_ranks:%s ", ranks.String())
		ranks = &ranklist.RankSet{}
		for _, r := range m.TgtRanks {
			ranks.Add(ranklist.Rank(r))
		}
		fmt.Fprintf(&bld, "tgt_ranks:%s ", ranks.String())
		fmt.Fprint(&bld, "tiers:")
		for i, b := range m.TierBytes {
			fmt.Fprintf(&bld, "%d: %s (%d)", i, humanize.Bytes(b), b)
		}
		fmt.Fprintf(&bld, "meta-file-size: %s (%d)", humanize.Bytes(m.MemFileBytes),
			m.MemFileBytes)
	case *mgmtpb.PoolQueryReq:
		fmt.Fprintf(&bld, "%T id:%s qm:%s", m, m.Id, daos.PoolQueryMask(m.QueryMask))
	case *mgmtpb.PoolQueryResp:
		fmt.Fprintf(&bld, "%T status:%s uuid:%s qm:%s map:%d tot(eng/tgts):%d/%d ver(p/u):%d/%d svc_ldr:%d ",
			m, daos.Status(m.Status), m.Uuid, daos.PoolQueryMask(m.QueryMask), m.Version,
			m.TotalEngines, m.TotalTargets, m.PoolLayoutVer, m.UpgradeLayoutVer, m.SvcLdr)
		ranks := &ranklist.RankSet{}
		for _, r := range m.SvcReps {
			ranks.Add(ranklist.Rank(r))
		}
		fmt.Fprintf(&bld, "svc_ranks:%s ", ranks.String())
		fmt.Fprintf(&bld, "ena_ranks:%s ", m.EnabledRanks)
		fmt.Fprintf(&bld, "dis_ranks:%s ", m.DisabledRanks)
		fmt.Fprintf(&bld, "dead_ranks:%s ", m.DeadRanks)
		fmt.Fprintf(&bld, "rebuild:%+v ", m.Rebuild)
		fmt.Fprintf(&bld, "tier_stats:%+v ", m.TierStats)
	case *mgmtpb.PoolDrainReq:
		fmt.Fprintf(&bld, "%T pool:%s", m, m.Id)
		fmt.Fprintf(&bld, "drain rank:%d", m.Rank)
	case *mgmtpb.PoolReintReq:
		fmt.Fprintf(&bld, "%T pool:%s", m, m.Id)
		fmt.Fprintf(&bld, "reintegrate rank:%d", m.Rank)
	case *mgmtpb.PoolExcludeReq:
		fmt.Fprintf(&bld, "%T pool:%s", m, m.Id)
		fmt.Fprintf(&bld, "exclude rank:%d", m.Rank)
	case *mgmtpb.PoolEvictReq:
		fmt.Fprintf(&bld, "%T pool:%s", m, m.Id)
		if len(m.Handles) > 0 {
			shortHdls := make([]string, 0, len(m.Handles))
			for _, h := range m.Handles {
				shortHdls = append(shortHdls, h[:8])
			}
			fmt.Fprintf(&bld, " handles:%s", strings.Join(shortHdls, ","))
		}
		if m.Destroy {
			fmt.Fprint(&bld, " destroy:true")
		}
		if m.ForceDestroy {
			fmt.Fprint(&bld, " force_destroy:true")
		}
		if m.Machine != "" {
			fmt.Fprintf(&bld, " machine:%s", m.Machine)
		}
	case *mgmtpb.ListPoolsResp:
		fmt.Fprintf(&bld, "%T%d %d pools:", m, m.DataVersion, len(m.Pools))
		for _, p := range m.Pools {
			fmt.Fprintf(&bld, " %s:%s", p.Label, p.State)
		}
	case *mgmtpb.JoinResp:
		fmt.Fprintf(&bld, "%T rank:%d (state:%s, local:%t) map:%d", m, m.Rank, m.State, m.LocalJoin, m.MapVersion)
	case *mgmtpb.GetAttachInfoResp:
		msRanks := ranklist.RankSetFromRanks(ranklist.RanksFromUint32(m.MsRanks))
		uriRanks := ranklist.NewRankSet()
		for _, ru := range m.RankUris {
			uriRanks.Add(ranklist.Rank(ru.Rank))
		}
		fmt.Fprintf(&bld, "%T@%d ms:%s ranks:%s client:%+v build:%s", m, m.DataVersion, msRanks.String(), uriRanks.String(), m.ClientNetHint, m.BuildInfo.BuildString())
	case *mgmtpb.LeaderQueryResp:
		fmt.Fprintf(&bld, "%T leader:%s Replica Set:%s Down Replicas:%s", m, m.CurrentLeader, strings.Join(m.Replicas, ","), strings.Join(m.DownReplicas, ","))
	case *sharedpb.ClusterEventReq:
		fmt.Fprintf(&bld, "%T seq:%d", m, m.Sequence)
		if m.Event == nil {
			break
		}
		fmt.Fprintf(&bld, " evt: ts:%s ", m.Event.Timestamp)
		switch m.Event.Id {
		case events.RASSwimRankDead.Uint32():
			fmt.Fprintf(&bld, "host %s@%d: rank %d is dead", m.Event.Hostname, m.Event.Incarnation, m.Event.Rank)
		case events.RASPoolRepsUpdate.Uint32():
			fmt.Fprintf(&bld, "pool %s: svc reps upd", m.Event.PoolUuid)
			if m.Event.ExtendedInfo == nil {
				break
			}
			if psi, ok := m.Event.ExtendedInfo.(*sharedpb.RASEvent_PoolSvcInfo); ok {
				svcRanks := ranklist.RankSetFromRanks(ranklist.RanksFromUint32(psi.PoolSvcInfo.SvcReps))
				fmt.Fprintf(&bld, ": %s", svcRanks.String())
			}
		default:
			fmt.Fprintf(&bld, "(%+v)", m.Event)
		}
	case *ctlpb.RanksResp, *mgmtpb.SystemStartResp, *mgmtpb.SystemStopResp, *mgmtpb.SystemExcludeResp, *mgmtpb.SystemEraseResp:
		fmt.Fprintf(&bld, "%T", m)
		if rg, ok := m.(interface{ GetResults() []*sharedpb.RankResult }); ok {
			resMap := make(map[string]*ranklist.RankSet)
			for _, res := range rg.GetResults() {
				if resMap[res.State] == nil {
					resMap[res.State] = ranklist.NewRankSet()
				}
				resMap[res.State].Add(ranklist.Rank(res.Rank))
			}
			for state, ranks := range resMap {
				fmt.Fprintf(&bld, " %s:%s", state, ranks.String())
			}
		}
	default:
		fmt.Fprintf(&bld, "%T", m)
		if vm, ok := m.(interface{ GetDataVersion() uint64 }); ok {
			fmt.Fprintf(&bld, "@%d", vm.GetDataVersion())
		}
		if sr, ok := m.(interface{ GetStatus() int32 }); ok {
			fmt.Fprintf(&bld, " status:%s", daos.Status(sr.GetStatus()))
		}
		dbg := fmt.Sprintf("%+v", m)
		if len(dbg) > 0 {
			fmt.Fprintf(&bld, " (%s)", dbg)
		}
	}

	return bld.String()
}
