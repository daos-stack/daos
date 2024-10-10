//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"fmt"

	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

// rankList defines a type constraint for acceptable rank list formats.
type rankList interface {
	[]uint32 | []ranklist.Rank
}

// PrintRanks returns a string representation of the given ranks.
func PrintRanks[rlt rankList](ranks rlt) string {
	switch rl := any(ranks).(type) {
	case []uint32:
		return ranklist.RankSetFromRanks(ranklist.RanksFromUint32(rl)).RangedString()
	case []ranklist.Rank:
		return ranklist.RankSetFromRanks(rl).RangedString()
	default:
		// NB: This should never happen, due to the compiler check of the type constraint.
		return fmt.Sprintf("unknown rank list type: %T", rl)
	}
}
