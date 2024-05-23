//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"io"

	"github.com/daos-stack/daos/src/control/lib/control"
)

// PrintSetEngineLogMasksResp generates a human-readable representation of the supplied response.
func PrintSetEngineLogMasksResp(resp *control.SetEngineLogMasksResp, out, outErr io.Writer) error {
	if err := PrintResponseErrors(resp, outErr); err != nil {
		return err
	}

	return PrintHostStorageSuccesses("Engine log-masks updated", resp.HostStorage, out)
}
