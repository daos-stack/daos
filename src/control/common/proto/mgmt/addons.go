//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package mgmt

import (
	"encoding/json"
	"fmt"
	"strings"

	"github.com/google/uuid"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/system"
)

func (p *PoolProperty) UnmarshalJSON(b []byte) error {
	type fromJSON PoolProperty
	from := struct {
		Value struct {
			Strval string `json:"strval"`
			Numval uint64 `json:"numval"`
		}
		*fromJSON
	}{
		fromJSON: (*fromJSON)(p),
	}

	if err := json.Unmarshal(b, &from); err != nil {
		return err
	}

	if from.Value.Strval != "" {
		p.SetValueString(from.Value.Strval)
	} else {
		p.SetValueNumber(from.Value.Numval)
	}

	return nil
}

// SetValueString sets the Value field to a string.
func (p *PoolProperty) SetValueString(strVal string) {
	p.Value = &PoolProperty_Strval{
		Strval: strVal,
	}
}

// SetValueNumber sets the Value field to a uint64.
func (p *PoolProperty) SetValueNumber(numVal uint64) {
	p.Value = &PoolProperty_Numval{
		Numval: numVal,
	}
}

// The following set of addons implements the poolServiceReq interface
// in mgmt_pool.go.

// SetUUID sets the request's ID to a UUID.
func (r *PoolDestroyReq) SetUUID(id uuid.UUID) {
	r.Id = id.String()
}

// SetUUID sets the request's ID to a UUID.
func (r *PoolUpgradeReq) SetUUID(id uuid.UUID) {
	r.Id = id.String()
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *PoolUpgradeReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *PoolSetPropReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetUUID sets the request's ID to a UUID.
func (r *PoolSetPropReq) SetUUID(id uuid.UUID) {
	r.Id = id.String()
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *PoolGetPropReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetUUID sets the request's ID to a UUID.
func (r *PoolGetPropReq) SetUUID(id uuid.UUID) {
	r.Id = id.String()
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *PoolEvictReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetUUID sets the request's ID to a UUID.
func (r *PoolEvictReq) SetUUID(id uuid.UUID) {
	r.Id = id.String()
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *PoolExcludeReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetUUID sets the request's ID to a UUID.
func (r *PoolExcludeReq) SetUUID(id uuid.UUID) {
	r.Id = id.String()
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *PoolDrainReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetUUID sets the request's ID to a UUID.
func (r *PoolDrainReq) SetUUID(id uuid.UUID) {
	r.Id = id.String()
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *PoolReintegrateReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetUUID sets the request's ID to a UUID.
func (r *PoolReintegrateReq) SetUUID(id uuid.UUID) {
	r.Id = id.String()
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *PoolExtendReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetUUID sets the request's ID to a UUID.
func (r *PoolExtendReq) SetUUID(id uuid.UUID) {
	r.Id = id.String()
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *PoolQueryReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetUUID sets the request's ID to a UUID.
func (r *PoolQueryReq) SetUUID(id uuid.UUID) {
	r.Id = id.String()
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *GetACLReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetUUID sets the request's ID to a UUID.
func (r *GetACLReq) SetUUID(id uuid.UUID) {
	r.Id = id.String()
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *ModifyACLReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetUUID sets the request's ID to a UUID.
func (r *ModifyACLReq) SetUUID(id uuid.UUID) {
	r.Id = id.String()
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *DeleteACLReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetUUID sets the request's ID to a UUID.
func (r *DeleteACLReq) SetUUID(id uuid.UUID) {
	r.Id = id.String()
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *ContSetOwnerReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetUUID sets the request's ID to a UUID.
func (r *ContSetOwnerReq) SetUUID(id uuid.UUID) {
	r.PoolUUID = id.String()
}

// GetId fetches the pool ID.
func (r *ContSetOwnerReq) GetId() string {
	return r.PoolUUID
}

func Debug(msg proto.Message) string {
	var bld strings.Builder
	switch m := msg.(type) {
	case *SystemQueryResp:
		stateRanks := make(map[string]*system.RankSet)
		for _, m := range m.Members {
			if _, found := stateRanks[m.State]; !found {
				stateRanks[m.State] = &system.RankSet{}
			}
			stateRanks[m.State].Add(system.Rank(m.Rank))
		}
		fmt.Fprintf(&bld, "%T ", m)
		for state, set := range stateRanks {
			fmt.Fprintf(&bld, "%s: %s ", state, set.String())
		}
	case *PoolCreateReq:
		fmt.Fprintf(&bld, "%T uuid:%s u:%s g:%s ", m, m.Uuid, m.User, m.Usergroup)
		if len(m.Properties) > 0 {
			fmt.Fprintf(&bld, "p:%+v ", m.Properties)
		}
		ranks := &system.RankSet{}
		for _, r := range m.Ranks {
			ranks.Add(system.Rank(r))
		}
		fmt.Fprintf(&bld, "ranks: %s ", ranks.String())
		fmt.Fprint(&bld, "tiers: ")
		for i, b := range m.Tierbytes {
			fmt.Fprintf(&bld, "%d: %d ", i, b)
			if len(m.Tierratio) > i+1 {
				fmt.Fprintf(&bld, "(%.02f%%) ", m.Tierratio[i])
			}
		}
	case *PoolCreateResp:
		fmt.Fprintf(&bld, "%T ", m)
		ranks := &system.RankSet{}
		for _, r := range m.SvcReps {
			ranks.Add(system.Rank(r))
		}
		fmt.Fprintf(&bld, "svc_ranks: %s ", ranks.String())
		ranks = &system.RankSet{}
		for _, r := range m.TgtRanks {
			ranks.Add(system.Rank(r))
		}
		fmt.Fprintf(&bld, "tgt_ranks: %s ", ranks.String())
		fmt.Fprint(&bld, "tiers: ")
		for i, b := range m.TierBytes {
			fmt.Fprintf(&bld, "%d: %d ", i, b)
		}
	default:
		return fmt.Sprintf("%+v", m)
	}

	return bld.String()
}
