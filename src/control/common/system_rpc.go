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

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
)

// MembersToPB converts internal member structs to protobuf equivalents.
func MembersToPB(members SystemMembers) []*mgmtpb.SystemMember {
	pbMembers := make([]*mgmtpb.SystemMember, 0, len(members))

	for _, m := range members {
		pbMembers = append(pbMembers, &mgmtpb.SystemMember{
			Addr: m.Addr.String(), Uuid: m.Uuid, Rank: m.Rank,
		})
	}

	return pbMembers
}

// MembersFromPB converts to member slice from protobuf format.
//
// Don't populate member Addr field if it can't be resolved.
func MembersFromPB(log logging.Logger, pbMembers []*mgmtpb.SystemMember) SystemMembers {
	members := make(SystemMembers, 0, len(pbMembers))

	for _, m := range pbMembers {
		var addr net.Addr

		addr, err := net.ResolveTCPAddr("tcp", m.Addr)
		if err != nil {
			log.Errorf("cannot resolve member address %s: %s", m, err)
			continue
		}

		members = append(members, &SystemMember{
			Addr: addr, Uuid: m.Uuid, Rank: m.Rank,
		})
	}

	return members
}

// MemberResultsToPB converts SystemMemberResults to equivalent protobuf format.
func MemberResultsToPB(results SystemMemberResults) []*mgmtpb.SystemStopResp_Result {
	pbResults := make([]*mgmtpb.SystemStopResp_Result, 0, len(results))

	for _, mr := range results {
		pbResults = append(pbResults, &mgmtpb.SystemStopResp_Result{
			Id: mr.ID, Action: mr.Action, Errored: mr.Err == nil, Msg: mr.Err.Error(),
		})
	}

	return pbResults
}

// MemberResultsFromPB converts results from member actions (protobuf format) to
// SystemMemberResults.
func MemberResultsFromPB(log logging.Logger, pbResults []*mgmtpb.SystemStopResp_Result) SystemMemberResults {
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
