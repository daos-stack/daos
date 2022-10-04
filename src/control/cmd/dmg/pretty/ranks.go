//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import "github.com/daos-stack/daos/src/control/lib/ranklist"

// formatRanks takes a slice of uint32 ranks and returns a string
// representation of the set created from the slice.
func formatRanks(ranks []uint32) string {
	rs := ranklist.RankSetFromRanks(ranklist.RanksFromUint32(ranks))
	return rs.RangedString()
}
