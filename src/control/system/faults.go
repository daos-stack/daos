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
package system

import (
	"fmt"

	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/fault/code"
)

func FaultMemberExists(m *Member) *fault.Fault {
	return systemFault(
		code.SystemMemberExists,
		fmt.Sprintf("system member with rank %d already exists (%v)", m.Rank, m),
		"update system member instead of adding",
	)
}

func FaultMemberMissing(rank Rank) *fault.Fault {
	return systemFault(
		code.SystemMemberMissing,
		fmt.Sprintf("system member with rank %d doesn't exists", rank),
		"",
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
