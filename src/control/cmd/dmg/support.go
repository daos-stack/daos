//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"fmt"
	"os"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/cmd/dmg/pretty"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/support"
)

// NetCmd is the struct representing the top-level network subcommand.
type SupportCmd struct {
	CollectLog collectLogCmd `command:"collectlog" description:"Collect logs from servers"`
}

// collectLogCmd is the struct representing the command to collect the logs from the servers for support purpose
type collectLogCmd struct {
	baseCmd
	cfgCmd
	ctlInvokerCmd
	hostListCmd
	jsonOutputCmd
	Stop         bool   `short:"s" long:"stop" description:"Stop the collectlog command on very first error"`
	TargetFolder string `short:"t" long:"target" description:"Target Folder location where log will be copied"`
	Archive      bool   `short:"z" long:"archive" description:"Archive the log/config files"`
	CustomLogs   string `short:"c" long:"custom-logs" description:"Collect the Logs from given directory"`
}

func (cmd *collectLogCmd) Execute(_ []string) error {
	// Default log collection set
	var LogCollection = map[string][]string{
		"CopyServerConfig":     {""},
		"CollectSystemCmd":     support.SystemCmd,
		"CollectServerLog":     support.ServerLog,
		"CollectDaosServerCmd": support.DaosServerCmd,
	}

	// Default 7 set of support collection steps to show in progress bar
	progress := support.ProgressBar{1, 7, 0, cmd.jsonOutputEnabled()}

	// Add custom log location
	if cmd.CustomLogs != "" {
		LogCollection["CollectCustomLogs"] = []string{""}
		progress.Total = progress.Total + 1
	}

	// Increase progress counter for Archive if enabled
	if cmd.Archive == true {
		progress.Total = progress.Total + 1
	}
	progress.Steps = 100 / progress.Total

	// Check if DAOS Management Service is up and running
	params := support.Params{}
	params.Config = cmd.cfgCmd.config.Path
	params.LogFunction = "CollectDmgCmd"
	params.LogCmd = "dmg system query"

	err := support.CollectSupportLog(cmd.Logger, params)

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(nil, err)
	}

	if err != nil {
		return errors.Wrap(err, "DAOS Management Service is down")
	}

	if cmd.TargetFolder == "" {
		cmd.TargetFolder = "/tmp/daos_support_server_logs"
	}
	cmd.Infof("Support logs will be copied to %s", cmd.TargetFolder)
	if err := os.Mkdir(cmd.TargetFolder, 0700); err != nil && !os.IsExist(err) {
		return err
	}

	// Copy log/config file to TargetFolder on all servers
	for logfunc, logcmdset := range LogCollection {
		for _, logcmd := range logcmdset {
			cmd.Debugf("Log Function %s -- Log Collect Cmd %s ", logfunc, logcmd)
			ctx := context.Background()
			req := &control.CollectLogReq{
				TargetFolder: cmd.TargetFolder,
				CustomLogs:   cmd.CustomLogs,
				LogFunction:  logfunc,
				LogCmd:       logcmd,
			}
			req.SetHostList(cmd.hostlist)
			resp, err := control.CollectLog(ctx, cmd.ctlInvoker, req)
			if err != nil && cmd.Stop == true {
				return err
			}
			if len(resp.GetHostErrors()) > 0 {
				var bld strings.Builder
				_ = pretty.PrintResponseErrors(resp, &bld)
				cmd.Info(bld.String())
				if cmd.Stop == true {
					return resp.Errors()
				}
			}
		}
		fmt.Printf(support.PrintProgress(&progress))
	}

	// Rsync the logs from servers
	hostName, _ := support.GetHostName()
	req := &control.CollectLogReq{
		TargetFolder: cmd.TargetFolder,
		LogFunction:  "rsyncLog",
		LogCmd:       hostName,
	}
	cmd.Debugf("Rsync logs from servers to %s:%s ", hostName, cmd.TargetFolder)
	resp, err := control.CollectLog(context.Background(), cmd.ctlInvoker, req)
	if err != nil && cmd.Stop == true {
		return err
	}
	if len(resp.GetHostErrors()) > 0 {
		var bld strings.Builder
		_ = pretty.PrintResponseErrors(resp, &bld)
		cmd.Info(bld.String())
		if cmd.Stop == true {
			return resp.Errors()
		}
	}
	fmt.Printf(support.PrintProgress(&progress))

	// Collect dmg command output on Admin node
	var DmgInfoCollection = map[string][]string{
		"CollectDmgCmd":      support.DmgCmd,
		"CollectDmgDiskInfo": {""},
	}

	params = support.Params{}
	params.Config = cmd.cfgCmd.config.Path
	params.TargetFolder = cmd.TargetFolder
	params.CustomLogs = cmd.CustomLogs
	params.JsonOutput = cmd.jsonOutputEnabled()
	params.Hostlist = strings.Join(cmd.hostlist, " ")
	for logfunc, logcmdset := range DmgInfoCollection {
		for _, logcmd := range logcmdset {
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

	// Archive the logs
	if cmd.Archive == true {
		cmd.Infof("Archiving the Log Folder %s", cmd.TargetFolder)
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
