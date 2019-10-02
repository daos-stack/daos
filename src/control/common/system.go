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
	"net"
	"sync"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
	log "github.com/daos-stack/daos/src/control/logging"
	"github.com/pkg/errors"
)

// SystemMember refers to a data-plane instance that is a member of this DAOS
// system running on host with the control-plane listening at "Addr".
type SystemMember struct {
	Addr net.Addr
	Uuid string
	Rank uint32
}

// Membership tracks details of system members.
type Membership struct {
	sync.Mutex
	log     logging.Logger
	members map[string]SystemMember
}

// Add adds member to membership.
func (m *Membership) Add(member SystemMember) error {
	m.Lock()
	defer m.Unlock()

	if value, found := m.members[member.Uuid]; found {
		return errors.Errorf("member %s already exists at %+v",
			member.Uuid, value)
	}

	m.members[member.Uuid] = member
	m.log.Debugf("system membership: %+v\n", m.members)

	return nil
}

// Remove removes member from membership.
func (m *Membership) Remove(uuid string) error {
	m.Lock()
	defer m.Unlock()

	if _, found := m.members[uuid]; !found {
		return errors.Errorf("member %s doesn't exist", uuid)
	}

	delete(m.members, uuid)

	return nil
}

// GetMember retrieves member from membership based on UUID.
func (m *Membership) GetMember(uuid string) SystemMember {
	m.Lock()
	defer m.Unlock()

	return m.members[uuid]
}

// GetMembers returns internal member structs as a sequence.
func (m *Membership) GetMembers() []*SystemMember {
	members := make([]*SystemMember, len(m.members))

	m.Lock()
	defer m.Unlock()

	for _, m := range m.members {
		members = append(members, &m)
	}

	return members
}

// GetMembersPB converts internal member structs to protobuf equivalents.
func (m *Membership) GetMembersPB() []*mgmtpb.SystemMember {
	pbMembers := make([]*mgmtpb.SystemMember, len(m.members))

	m.Lock()
	defer m.Unlock()

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
func MembersFromPB(pbMembers []*mgmtpb.SystemMember) []*SystemMember {
	members := make([]*SystemMember, len(pbMembers))

	for _, m := range pbMembers {
		var addr net.Addr

		addr, err := net.ResolveTCPAddr("tcp", m.Addr)
		if err != nil {
			// leave addr as zero net.Addr value
			log.Errorf("resolving tcp address %s: %s", m.Addr, err)
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
