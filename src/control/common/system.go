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
	"fmt"
	"net"
	"strings"
	"sync"

	"github.com/pkg/errors"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
)

// SystemMember refers to a data-plane instance that is a member of this DAOS
// system running on host with the control-plane listening at "Addr".
type SystemMember struct {
	Addr net.Addr
	Uuid string
	Rank uint32
}

func (sm SystemMember) String() string {
	return fmt.Sprintf("%s/%s/%d", sm.Addr, sm.Uuid, sm.Rank)
}

type SystemMembers []*SystemMember

func (sms SystemMembers) String() string {
	var sb strings.Builder

	for _, m := range sms {
		sb.WriteString(m.String() + " ")
	}

	return sb.String()
}

// Membership tracks details of system members.
type Membership struct {
	sync.RWMutex
	log     logging.Logger
	members map[string]SystemMember
}

// Add adds member to membership.
func (m *Membership) Add(member SystemMember) (int, error) {
	m.Lock()
	defer m.Unlock()

	if value, found := m.members[member.Uuid]; found {
		return -1, errors.Errorf("member %s already exists", value)
	}

	m.members[member.Uuid] = member

	return len(m.members), nil
}

// Remove removes member from membership, idenpotent.
//
// Avoid taking a RW lock where possible.
func (m *Membership) Remove(uuid string) {
	m.Lock()
	defer m.Unlock()

	delete(m.members, uuid)
}

// GetMember retrieves member from membership based on UUID.
func (m *Membership) GetMember(uuid string) (*SystemMember, error) {
	m.RLock()
	defer m.RUnlock()

	member, found := m.members[uuid]
	if !found {
		return nil, errors.Errorf("member with uuid %s not found", uuid)
	}

	return &member, nil
}

// GetMembers returns internal member structs as a sequence.
func (m *Membership) GetMembers() SystemMembers {
	members := make([]*SystemMember, 0, len(m.members))

	m.RLock()
	defer m.RUnlock()

	for _, m := range m.members {
		members = append(members, &m)
	}

	return members
}

// GetMembersPB converts internal member structs to protobuf equivalents.
func (m *Membership) GetMembersPB() []*mgmtpb.SystemMember {
	pbMembers := make([]*mgmtpb.SystemMember, 0, len(m.members))

	m.RLock()
	defer m.RUnlock()

	for _, m := range m.members {
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
	members := make([]*SystemMember, len(pbMembers))

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

func NewMembership(log logging.Logger) *Membership {
	return &Membership{members: make(map[string]SystemMember), log: log}
}
