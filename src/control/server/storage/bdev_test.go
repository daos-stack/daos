//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"fmt"
	"testing"

	"github.com/daos-stack/daos/src/control/common"
)

func Test_NvmeDevState(t *testing.T) {
	// verify "NEW" state
	state := NvmeStatePlugged
	common.AssertTrue(t, state.IsNew(), "expected state to indicate new")
	common.AssertFalse(t, state.IsNormal(), "expected state to not indicate normal")
	common.AssertFalse(t, state.IsFaulty(), "expected state to not indicate faulty")
	common.AssertTrue(t, NvmeDevStateFromString(state.StatusString()) == state,
		fmt.Sprintf("expected string %s to yield state %s", state.StatusString(),
			NvmeDevStateFromString(state.StatusString()).StatusString()))

	// verify "NORMAL" state
	state = NvmeStatePlugged | NvmeStateInUse
	common.AssertFalse(t, state.IsNew(), "expected state to not indicate new")
	common.AssertTrue(t, state.IsNormal(), "expected state to indicate normal")
	common.AssertFalse(t, state.IsFaulty(), "expected state to not indicate faulty")
	common.AssertTrue(t, NvmeDevStateFromString(state.StatusString()) == state,
		fmt.Sprintf("expected string %s to yield state %s", state.StatusString(),
			NvmeDevStateFromString(state.StatusString()).StatusString()))

	// verify "EVICTED" state
	state = NvmeStatePlugged | NvmeStateInUse | NvmeStateFaulty
	common.AssertFalse(t, state.IsNew(), "expected state to not indicate new")
	common.AssertFalse(t, state.IsNormal(), "expected state to not indicate normal")
	common.AssertTrue(t, state.IsFaulty(), "expected state to indicate faulty")
	common.AssertTrue(t, NvmeDevStateFromString(state.StatusString()) == state,
		fmt.Sprintf("expected string %s to yield state %s", state.StatusString(),
			NvmeDevStateFromString(state.StatusString()).StatusString()))

	// verify "NEW|EVICTED" state
	state = NvmeStatePlugged | NvmeStateFaulty
	common.AssertTrue(t, state.IsNew(), "expected state to indicate new")
	common.AssertFalse(t, state.IsNormal(), "expected state to not indicate normal")
	common.AssertTrue(t, state.IsFaulty(), "expected state to indicate faulty")
	common.AssertTrue(t, NvmeDevStateFromString(state.StatusString()) == state,
		fmt.Sprintf("expected string %s to yield state %s", state.StatusString(),
			NvmeDevStateFromString(state.StatusString()).StatusString()))

	// verify "UNPLUGGED|NEW|EVICTED|IDENTIFY" state
	state = NvmeStateFaulty | NvmeStateIdentify
	common.AssertFalse(t, state.IsNew(), "expected state to not indicate new")
	common.AssertFalse(t, state.IsNormal(), "expected state to not indicate normal")
	common.AssertFalse(t, state.IsFaulty(), "expected state to not indicate faulty")
	common.AssertTrue(t, NvmeDevStateFromString(state.StatusString()) == state,
		fmt.Sprintf("expected string %s to yield state %s", state.StatusString(),
			NvmeDevStateFromString(state.StatusString()).StatusString()))

	// verify "NEW|EVICTED|IDENTIFY" state
	state = NvmeStatePlugged | NvmeStateFaulty | NvmeStateIdentify
	common.AssertTrue(t, state.IsNew(), "expected state to indicate new")
	common.AssertFalse(t, state.IsNormal(), "expected state to not indicate normal")
	common.AssertTrue(t, state.IsFaulty(), "expected state to indicate faulty")
	common.AssertTrue(t, NvmeDevStateFromString(state.StatusString()) == state,
		fmt.Sprintf("expected string %s to yield state %s", state.StatusString(),
			NvmeDevStateFromString(state.StatusString()).StatusString()))
}
