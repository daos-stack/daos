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
	"sync"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/pkg/errors"
)

// SystemMember refers to a data-plane instance that is a member of this DAOS
// system running on host with the control-plane listening at "Addr".
type SystemMember struct {
	Addr string
	Uuid string
	Rank uint32
}

// Membership tracks details of system members.
type Membership struct {
	sync.RWMutex
	log     logging.Logger
	members map[string]SystemMember
}

// Add adds member to membership.
func (m *Membership) Add(member SystemMember) error {
	m.RLock()
	value, found := m.members[member.Uuid]
	m.RUnlock()
	if found {
		return errors.Errorf("member %s already exists (%+v)",
			member.Uuid, value)
	}

	m.Lock()
	m.members[member.Uuid] = member
	m.Unlock()

	return nil
}

// Remove removes member from membership.
func (m *Membership) Remove(uuid string) error {
	m.RLock()
	_, found := m.members[uuid]
	m.RUnlock()
	if !found {
		return errors.Errorf("member %s doesn't exist", uuid)
	}

	m.Lock()
	delete(m.members, uuid)
	m.Unlock()

	return nil
}

// GetMember retrieves member from membership based on UUID.
func (m *Membership) GetMember(uuid string) SystemMember {
	m.RLock()
	defer m.RUnlock()

	return m.members[uuid]
}

// GetMembers returns internal member structs as a sequence.
func (m *Membership) GetMembers() []*SystemMember {
	members := make([]*SystemMember, len(m.members))

	m.RLock()
	defer m.RUnlock()

	for _, m := range m.members {
		members = append(members, &m)
	}

	return members
}

// GetMembersPB converts internal member structs to protobuf equivalents.
func (m *Membership) GetMembersPB() []*mgmtpb.SystemMember {
	pbMembers := make([]*mgmtpb.SystemMember, len(m.members))

	m.RLock()
	defer m.RUnlock()

	for _, m := range m.members {
		pbMembers = append(pbMembers, &mgmtpb.SystemMember{
			Addr: m.Addr, Uuid: m.Uuid, Rank: m.Rank,
		})
	}

	return pbMembers
}

// MembersFromPB converts to member slice from protobuf format.
func MembersFromPB(pbMembers []*mgmtpb.SystemMember) []*SystemMember {
	members := make([]*SystemMember, len(pbMembers))

	for _, m := range pbMembers {
		members = append(members, &SystemMember{
			Addr: m.Addr, Uuid: m.Uuid, Rank: m.Rank,
		})
	}

	return members
}

func NewMembership(log logging.Logger) *Membership {
	return &Membership{members: make(map[string]SystemMember), log: log}
}
