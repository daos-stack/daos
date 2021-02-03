//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import "github.com/daos-stack/daos/src/control/system"

// FormatRanks takes a slice of uint32 ranks and returns a string
// representation of the set created from the slice.
func FormatRanks(ranks []uint32) string {
	rs := system.RankSetFromRanks(system.RanksFromUint32(ranks))
	return rs.RangedString()
}
