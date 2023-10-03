//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"errors"
	"fmt"
	"io"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
)

// PrintSetEngineLogMasksResp generates a human-readable representation of the supplied response.
func PrintSetEngineLogMasksResp(resp *control.SetEngineLogMasksResp, out, outErr io.Writer) error {
	if err := PrintResponseErrors(resp, outErr); err != nil {
		return err
	}

	switch len(resp.HostStorage) {
	case 0:
	case 1:
		for _, hss := range resp.HostStorage {
			fmt.Fprintf(out,
				"Engine log-masks updated successfully on the following %s: %s\n",
				common.Pluralise("host", hss.HostSet.Count()), hss.HostSet)
		}
	default:
		return errors.New("unexpected number of keys in HostStorageMap")
	}

	return nil
}
