//
// (C) Copyright 2019 Intel Corporation.
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

package ioserver

import (
	"math"
	"strconv"

	"github.com/pkg/errors"
)

// Rank is used to uniquely identify a server within a cluster
type Rank uint32

const (
	// MaxRank is the largest valid Rank value
	MaxRank Rank = math.MaxUint32 - 1
	// NilRank is an unset Rank (0 is a valid Rank)
	NilRank Rank = math.MaxUint32
)

func NewRankPtr(in uint32) *Rank {
	r := Rank(in)
	return &r
}

func (r *Rank) String() string {
	switch {
	case r == nil:
		return "nil"
	case *r == NilRank:
		return "NilRank"
	default:
		return strconv.FormatUint(uint64(*r), 10)
	}
}

func (r *Rank) Uint32() uint32 {
	if r == nil {
		return uint32(NilRank)
	}
	return uint32(*r)
}

func (r *Rank) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var i uint32
	if err := unmarshal(&i); err != nil {
		return err
	}
	if err := checkRank(Rank(i)); err != nil {
		return err
	}
	*r = Rank(i)
	return nil
}

// Equals compares this rank to the given rank. If either value is
// nil, the comparison is always false.
func (r *Rank) Equals(other *Rank) bool {
	if r == nil || other == nil {
		return false
	}
	return *r == *other
}

func checkRank(r Rank) error {
	if r == NilRank {
		return errors.Errorf("rank %d out of range [0, %d]", r, MaxRank)
	}
	return nil
}
