//
// (C) Copyright 2021-2022 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"fmt"
	"time"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
)

// FaultBadFaultDomainDepth generates a fault indicating that the server could
// not join because its fault domain depth was inconsistent with already-joined
// members.
func FaultBadFaultDomainDepth(domain *FaultDomain, expDepth int) *fault.Fault {
	return systemFault(code.SystemBadFaultDomainDepth,
		fmt.Sprintf("cannot join system with fault domain %q, need layers = %d", domain.String(), expDepth),
		"reconfigure the fault domain with a depth consistent with other system members, and restart the server")
}

// FaultPoolLocked generates a fault indicating that the pool is locked.
func FaultPoolLocked(poolUUID, lockID uuid.UUID, lockTime time.Time) *fault.Fault {
	return systemFault(code.SystemPoolLocked,
		fmt.Sprintf("pool %s is locked (id: %s, time: %s)", poolUUID, lockID, common.FormatTime(lockTime)),
		"retry the pool operation")
}

func FaultJoinReplaceRankNotFound(nrFieldsNotMatching int) *fault.Fault {
	suggestionMsg := "check that dmg format --replace is being run on a host with an engine " +
		"that has previously had a rank excluded from the system"
	if nrFieldsNotMatching > 1 {
		suggestionMsg = "engines on selected storage server don't seem to match any " +
			"existing records in the management service, run dmg storage format " +
			"without the --replace option"
	}

	return systemFault(
		code.SystemJoinReplaceRankNotFound,
		"system member could not be found from join request fields",
		suggestionMsg,
	)
}

func systemFault(code code.Code, desc, res string) *fault.Fault {
	return &fault.Fault{
		Domain:      "system",
		Code:        code,
		Description: desc,
		Resolution:  res,
	}
}
