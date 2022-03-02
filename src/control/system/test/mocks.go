//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package test

import (
	"fmt"
	"net"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
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
func MockMember(t *testing.T, idx uint32, state system.MemberState, info ...string) *system.Member {
	addr := MockControlAddr(t, idx)
	m := system.NewMember(system.Rank(idx), test.MockUUID(int32(idx)),
		addr.String(), addr, state)
	m.FabricContexts = idx
	if len(info) > 0 {
		m.Info = info[0]
	}
	return m
}

// MockMemberResult return a result from an action on a system member.
func MockMemberResult(rank system.Rank, action string, err error, state system.MemberState) *system.MemberResult {
	result := system.NewMemberResult(rank, err, state)
	result.Action = action

	return result
}

func MockMembership(t *testing.T, log logging.Logger, mdb system.MemberStore, resolver system.TCPResolver) *system.Membership {
	m := system.NewMembership(log, mdb)

	if resolver != nil {
		return m.WithTCPResolver(resolver)
	}

	return m
}
