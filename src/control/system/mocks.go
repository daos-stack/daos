//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"fmt"
	"net"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

func MockControlAddr(t *testing.T, idx uint32) *net.TCPAddr {
	addr, err := net.ResolveTCPAddr("tcp",
		fmt.Sprintf("127.0.0.%d:10001", idx))
	if err != nil {
		t.Fatal(err)
	}
	return addr
}

// MockMember returns a system member with appropriate values.
func MockMember(t *testing.T, idx uint32, state MemberState, info ...string) *Member {
	addr := MockControlAddr(t, idx)
	m, err := NewMember(Rank(idx), test.MockUUID(int32(idx)),
		addr.String(), addr, state)
	if err != nil {
		t.Fatal(err)
	}
	m.FabricContexts = idx
	if len(info) > 0 {
		m.Info = info[0]
	}
	return m
}

// MockMemberResult return a result from an action on a system member.
func MockMemberResult(rank Rank, action string, err error, state MemberState) *MemberResult {
	result := NewMemberResult(rank, err, state)
	result.Action = action

	return result
}

// MockMembership returns an initialized *Membership using the given MemberStore.
func MockMembership(t *testing.T, log logging.Logger, mdb MemberStore, resolver TCPResolver) *Membership {
	m := NewMembership(log, mdb)

	if resolver != nil {
		return m.WithTCPResolver(resolver)
	}

	return m
}
