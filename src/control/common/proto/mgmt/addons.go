//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package mgmt

import (
	"encoding/json"
	"fmt"

	"github.com/google/uuid"
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
func (r *PoolQueryTargetReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetUUID sets the request's ID to a UUID.
func (r *PoolQueryTargetReq) SetUUID(id uuid.UUID) {
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

// SetSvcRanks sets the request's Pool Service Ranks.
func (r *ListContReq) SetSvcRanks(rl []uint32) {
	r.SvcRanks = rl
}

// SetUUID sets the request's ID to a UUID.
func (r *ListContReq) SetUUID(id uuid.UUID) {
	r.Id = id.String()
}

func (bi *BuildInfo) BuildString() string {
	if bi == nil {
		return ""
	}

	baseString := bi.VersionString()
	if bi.Tag != "" {
		baseString += " (" + bi.Tag + ")"
	}
	return baseString
}

func (bi *BuildInfo) VersionString() string {
	if bi == nil {
		return ""
	}

	return fmt.Sprintf("%d.%d.%d", bi.Major, bi.Minor, bi.Patch)
}
