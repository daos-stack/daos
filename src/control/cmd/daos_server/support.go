//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"os"

	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/support"
)

type SupportCmd struct {
	CollectLog collectLogCmd `command:"collectlog" description:"Collect logs from server"`
}

// collectLogCmd is the struct representing the command to collect the Logs/config for support purpose
type collectLogCmd struct {
	cfgCmd
	cmdutil.LogCmd
	Stop         bool   `short:"s" long:"Stop" description:"Stop the collectlog command on very first error"`
	TargetFolder string `short:"t" long:"loglocation" description:"Folder location where log is going to be copied"`
	Archive      bool   `short:"z" long:"archive" description:"Archive the log/config files"`
	CustomLogs   string `short:"c" long:"custom-logs" description:"Collect the Logs from given directory"`
}

func (cmd *collectLogCmd) Execute(_ []string) error {
	var LogCollection = map[string][]string{
		"CopyServerConfig":     {""},
		"CollectSystemCmd":     support.SystemCmd,
		"CollectServerLog":     support.ServerLog,
		"CollectDaosServerCmd": support.DaosServerCmd,
	}

	// Default 4 steps of log/conf collection.
	progress := support.ProgressBar{1, 4, 0, false}

	if cmd.Archive == true {
		progress.Total = progress.Total + 1
	}

	// Copy the custom log location
	if cmd.CustomLogs != "" {
		LogCollection["CollectCustomLogs"] = []string{""}
		progress.Total = progress.Total + 1
	}

	if cmd.TargetFolder == "" {
		cmd.TargetFolder = "/tmp/daos_support_server_logs"
	}
	cmd.Infof("Support logs will be copied to %s", cmd.TargetFolder)

	progress.Steps = 100 / progress.Total
	params := support.Params{}
	params.Config = cmd.configPath()
	params.TargetFolder = cmd.TargetFolder
	params.CustomLogs = cmd.CustomLogs
	for logfunc, logcmdset := range LogCollection {
		for _, logcmd := range logcmdset {
			cmd.Debugf("Log Function %s -- Log Collect Cmd %s ", logfunc, logcmd)
			params.LogFunction = logfunc
			params.LogCmd = logcmd

			err := support.CollectSupportLog(cmd.Logger, params)
			if err != nil {
				fmt.Println(err)
				if cmd.Stop == true {
					return err
				}
			}
		}
		fmt.Printf(support.PrintProgress(&progress))
	}

	if cmd.Archive == true {
		cmd.Debugf("Archiving the Log Folder %s", cmd.TargetFolder)
		err := support.ArchiveLogs(cmd.Logger, params)
		if err != nil {
			return err
		}

		for i := 1; i < 3; i++ {
			os.RemoveAll(cmd.TargetFolder)
		}
	}

	fmt.Printf(support.PrintProgressEnd(&progress))

	return nil
}
