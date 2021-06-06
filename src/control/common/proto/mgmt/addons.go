//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package mgmt

import (
	"fmt"
	"strings"

	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/system"
)

// SetPropertyName sets the Property field to a string-based name.
func (r *PoolSetPropReq) SetPropertyName(name string) {
	r.Property = &PoolSetPropReq_Name{
		Name: name,
	}
}

// SetPropertyNumber sets the Property field to a uint32-based number.
func (r *PoolSetPropReq) SetPropertyNumber(number uint32) {
	r.Property = &PoolSetPropReq_Number{
		Number: number,
	}
}

// SetValueString sets the Value field to a string.
func (r *PoolSetPropReq) SetValueString(strVal string) {
	r.Value = &PoolSetPropReq_Strval{
		Strval: strVal,
	}
}

// SetValueNumber sets the Value field to a uint64.
func (r *PoolSetPropReq) SetValueNumber(numVal uint64) {
	r.Value = &PoolSetPropReq_Numval{
		Numval: numVal,
	}
}

// The following set of addons implements the poolServiceReq interface
// in mgmt_pool.go.

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *PoolSetPropReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *PoolEvictReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *PoolExcludeReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *PoolDrainReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *PoolReintegrateReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *PoolExtendReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *PoolQueryReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *GetACLReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *ModifyACLReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *DeleteACLReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

func Debug(msg proto.Message) string {
	switch t := msg.(type) {
	case *SystemQueryResp:
		stateRanks := make(map[string]*system.RankSet)
		for _, m := range t.Members {
			if _, found := stateRanks[m.State]; !found {
				stateRanks[m.State] = &system.RankSet{}
			}
			stateRanks[m.State].Add(system.Rank(m.Rank))
		}
		var bld strings.Builder
		fmt.Fprintf(&bld, "%T ", t)
		for state, set := range stateRanks {
			fmt.Fprintf(&bld, "%s: %s ", state, set.String())
		}
		return bld.String()
	default:
		return fmt.Sprintf("%+v", t)
	}
}
