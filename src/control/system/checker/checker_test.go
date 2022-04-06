//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package checker_test

import (
	"github.com/daos-stack/daos/src/control/system/checker"
	"github.com/daos-stack/daos/src/control/system/raft"
)

var _ checker.FindingStore = (*MockFindingStore)(nil)

type MockFindingStore struct {
	raft.InMemCheckerDatabase
}
