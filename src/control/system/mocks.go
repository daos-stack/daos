//
// (C) Copyright 2020-2022 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"fmt"
	"net"
	"testing"
	"time"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	. "github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
)

func MockControlAddr(t *testing.T, idx uint32) *net.TCPAddr {
	t.Helper()

	addr, err := net.ResolveTCPAddr("tcp",
		fmt.Sprintf("127.0.0.%d:10001", idx))
	if err != nil {
		t.Fatal(err)
	}

	return addr
}

// MockMemberFullSpec returns a reference to a new member struct.
func MockMemberFullSpec(t *testing.T, rank Rank, uuidStr string, uri string, addr *net.TCPAddr, state MemberState) *Member {
	t.Helper()

	newUUID, err := uuid.Parse(uuidStr)
	if err != nil {
		t.Fatal(err)
	}

	return &Member{
		Rank:             rank,
		UUID:             newUUID,
		PrimaryFabricURI: uri,
		Addr:             addr,
		State:            state,
		FaultDomain:      MustCreateFaultDomain(),
		LastUpdate:       time.Now(),
	}
}

// MockMember returns a system member with appropriate values.
func MockMember(t *testing.T, idx uint32, state MemberState, info ...string) *Member {
	t.Helper()

	addr := MockControlAddr(t, idx)
	m := MockMemberFullSpec(t, Rank(idx), test.MockUUID(int32(idx)), addr.String(), addr, state)
	m.PrimaryFabricContexts = idx
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

func MockResolveFn(netString string, address string) (*net.TCPAddr, error) {
	if netString != "tcp" {
		return nil, errors.Errorf("unexpected network type in test: %s, want 'tcp'", netString)
	}

	return map[string]*net.TCPAddr{
			"127.0.0.1:10001": {IP: net.ParseIP("127.0.0.1"), Port: 10001},
			"127.0.0.2:10001": {IP: net.ParseIP("127.0.0.2"), Port: 10001},
			"127.0.0.3:10001": {IP: net.ParseIP("127.0.0.3"), Port: 10001},
			"foo-1:10001":     {IP: net.ParseIP("127.0.0.1"), Port: 10001},
			"foo-2:10001":     {IP: net.ParseIP("127.0.0.2"), Port: 10001},
			"foo-3:10001":     {IP: net.ParseIP("127.0.0.3"), Port: 10001},
			"foo-4:10001":     {IP: net.ParseIP("127.0.0.4"), Port: 10001},
			"foo-5:10001":     {IP: net.ParseIP("127.0.0.5"), Port: 10001},
		}[address], map[string]error{
			"127.0.0.4:10001": errors.New("bad lookup"),
			"127.0.0.5:10001": errors.New("bad lookup"),
			"foo-6:10001":     errors.New("bad lookup"),
		}[address]
}

// MockMembership returns an initialized *Membership using the given MemberStore.
func MockMembership(t *testing.T, log logging.Logger, mdb MemberStore, resolver TCPResolver) *Membership {
	t.Helper()

	m := NewMembership(log, mdb)

	if resolver == nil {
		resolver = MockResolveFn
	}

	return m.WithTCPResolver(resolver)
}
