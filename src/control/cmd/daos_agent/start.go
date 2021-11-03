//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"

	"github.com/daos-stack/daos/src/control/lib/agent"
)

type startCmd struct {
	logCmd
	configCmd
	ctlInvokerCmd
}

func (cmd *startCmd) Execute(_ []string) error {
	agent := agent.NewServer(cmd.cfg, cmd.ctlInvoker)

	return agent.Start(context.Background(), cmd.log)
}
