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

package common

import (
	"errors"
	"net"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/logging"
)

// MembersToPB converts internal member structs to protobuf equivalents.
func MembersToPB(members SystemMembers) (pbMembers []*ctlpb.SystemMember, err error) {
	pbMembers = make([]*ctlpb.SystemMember, len(members))

	for _, m := range members {
		pbMembers = append(pbMembers, &ctlpb.SystemMember{
			Addr: m.Addr.String(), Uuid: m.UUID, Rank: m.Rank,
		})
	}

	return
}

// MembersFromPB converts to member slice from protobuf format.
//
// Don't populate member Addr field if it can't be resolved.
func MembersFromPB(log logging.Logger, pbMembers []*ctlpb.SystemMember) (members SystemMembers, err error) {
	var addr net.Addr
	members = make(SystemMembers, len(pbMembers))

	for _, m := range pbMembers {
		addr, err = net.ResolveTCPAddr("tcp", m.Addr)
		if err != nil {
			return
		}

		members = append(members, &SystemMember{
			Addr: addr, UUID: m.Uuid, Rank: m.Rank,
		})
	}

	return
}

// MemberResultsToPB converts SystemMemberResults to equivalent protobuf format.
func MemberResultsToPB(results SystemMemberResults) []*ctlpb.SystemStopResp_Result {
	pbResults := make([]*ctlpb.SystemStopResp_Result, 0, len(results))

	for _, r := range results {
		pbResult := &ctlpb.SystemStopResp_Result{Id: r.ID, Action: r.Action}
		if r.Err != nil {
			pbResult.Errored = true
			pbResult.Msg = r.Err.Error()
		}
		pbResults = append(pbResults, pbResult)
	}

	return pbResults
}

// MemberResultsFromPB converts results from member actions (protobuf format) to
// SystemMemberResults.
func MemberResultsFromPB(log logging.Logger, pbResults []*ctlpb.SystemStopResp_Result) SystemMemberResults {
	results := make(SystemMemberResults, 0, len(pbResults))

	for _, mr := range pbResults {
		var err error
		if mr.Errored {
			err = errors.New(mr.Msg)
		}

		results = append(results, &SystemMemberResult{
			ID: mr.Id, Action: mr.Action, Err: err,
		})
	}

	return results
}
