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

package proto

import (
	"net"

	"github.com/pkg/errors"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

// MembersToPB converts internal member structs to protobuf equivalents.
func MembersToPB(members system.Members) (pbMembers []*ctlpb.SystemMember, err error) {
	pbMembers = make([]*ctlpb.SystemMember, 0, len(members))

	for i, m := range members {
		if m == nil {
			return nil, errors.Errorf("nil member at index %d", i)
		}

		pbMembers = append(pbMembers, &ctlpb.SystemMember{
			Addr:  m.Addr.String(),
			Uuid:  m.UUID,
			Rank:  m.Rank,
			State: uint32(m.State()),
		})
	}

	return
}

// MembersFromPB converts to member slice from protobuf format.
//
// Don't populate member Addr field if it can't be resolved.
func MembersFromPB(log logging.Logger, pbMembers []*ctlpb.SystemMember) (members system.Members, err error) {
	var addr net.Addr
	members = make(system.Members, 0, len(pbMembers))

	for _, m := range pbMembers {
		addr, err = net.ResolveTCPAddr("tcp", m.Addr)
		if err != nil {
			return
		}

		members = append(members, system.NewMember(m.Rank, m.Uuid, addr,
			system.MemberState(m.State)))
	}

	return
}

// MemberResultsToPB converts system.MemberResults to equivalent protobuf format.
func MemberResultsToPB(results system.MemberResults) []*ctlpb.SystemStopResp_Result {
	pbResults := make([]*ctlpb.SystemStopResp_Result, 0, len(results))

	for _, r := range results {
		pbResult := &ctlpb.SystemStopResp_Result{Rank: r.Rank, Action: r.Action}
		if r.Err != nil {
			pbResult.Errored = true
			pbResult.Msg = r.Err.Error()
		}
		pbResults = append(pbResults, pbResult)
	}

	return pbResults
}

// MemberResultsFromPB converts results from member actions (protobuf format) to
// system.MemberResults.
func MemberResultsFromPB(log logging.Logger, pbResults []*ctlpb.SystemStopResp_Result) system.MemberResults {
	results := make(system.MemberResults, 0, len(pbResults))

	for _, mr := range pbResults {
		var err error
		if mr.Errored {
			err = errors.New(mr.Msg)
		}

		results = append(results, system.NewMemberResult(mr.Rank, mr.Action, err))
	}

	return results
}
