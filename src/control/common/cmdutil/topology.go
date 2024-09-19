//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package cmdutil

import (
	"context"
	"os"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/defaults/topology"
)

// DumpTopologyCmd implements a go-flags Commander that dumps
// the system topology to stdout or to a file.
type DumpTopologyCmd struct {
	JSONOutputCmd
	LogCmd
	Output string `short:"o" long:"output" default:"stdout" description:"Dump output to this location"`
}

func (cmd *DumpTopologyCmd) Execute(_ []string) error {
	out := os.Stdout
	if cmd.Output != "stdout" {
		f, err := os.Create(cmd.Output)
		if err != nil {
			return errors.Wrapf(err, "failed to create %q", cmd.Output)
		}
		defer f.Close()
		out = f
	}

	hwProv := topology.DefaultProvider(cmd.Logger)
	topo, err := hwProv.GetTopology(context.Background())
	if err != nil {
		return err
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(topo, err)
	}

	return hardware.PrintTopology(topo, out)
}
