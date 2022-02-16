//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"bytes"
	"context"
	"fmt"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
)

type systemCheckCmd struct {
	logCmd
	cfgCmd
	ctlInvokerCmd
	jsonOutputCmd

	Abort   bool `short:"a" long:"abort" description:"Abort checker process"`
	Query   bool `short:"q" long:"query" description:"Query checker status"`
	Restart bool `short:"r" long:"restart" description:"Reset checker data and restart from beginning"`
}

func (cmd *systemCheckCmd) printStatus(status *control.SystemCheckerStatusResp) {
	cmd.log.Infof("Current Pass: %s", status.CurrentPass)

	if len(status.Findings) == 0 {
		cmd.log.Infof("No findings")
		return
	}

	var buf bytes.Buffer
	iw := txtfmt.NewIndentWriter(&buf)
	cmd.log.Info("Findings:")
	for _, finding := range status.Findings {
		fmt.Fprintf(iw, "%s: %s\n", finding.Class, finding.Description)
		if len(finding.Resolutions) == 0 {
			continue
		}
		fmt.Fprintf(iw, "Potential resolutions:\n")
		iw2 := txtfmt.NewIndentWriter(iw)
		for _, resolution := range finding.Resolutions {
			fmt.Fprintf(iw2, "%d: %s\n", resolution.ID, resolution.Description)
		}
		fmt.Fprintln(&buf)
	}
	cmd.log.Info(buf.String())
}

func (cmd *systemCheckCmd) Execute(_ []string) (errOut error) {
	defer func() {
		errOut = errors.Wrap(errOut, "system check failed")
	}()

	if cmd.config == nil {
		return errors.New("no configuration loaded")
	}

	ctx := context.Background()
	status, err := control.SystemCheckerQuery(ctx, cmd.ctlInvoker)
	if err != nil {
		return err
	}

	if cmd.Query {
		if cmd.jsonOutputEnabled() {
			return cmd.outputJSON(status, err)
		}

		cmd.printStatus(status)
		return nil
	}

	status, err = control.SystemCheckerStart(ctx, cmd.ctlInvoker)
	if err != nil {
		return err
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(status, err)
	}

	cmd.printStatus(status)

	return nil
}
