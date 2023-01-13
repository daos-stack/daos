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
	support.CollectLogSubCmd
}

func (cmd *collectLogCmd) Execute(_ []string) error {
	// Default log collection set
	var LogCollection = map[int32][]string{
		support.CopyServerConfigEnum:     {""},
		support.CollectSystemCmdEnum:     support.SystemCmd,
		support.CollectServerLogEnum:     support.ServerLog,
		support.CollectDaosServerCmdEnum: support.DaosServerCmd,
	}

	// Default 7 set of support collection steps to show in progress bar
	progress := support.ProgressBar{1, 7, 0, cmd.jsonOutputEnabled()}

	// Add custom log location
	if cmd.CustomLogs != "" {
		LogCollection[support.CollectCustomLogsEnum] = []string{""}
		progress.Total++
	}

	// Increase progress counter for Archive if enabled
	if cmd.Archive {
		progress.Total++
	}
	progress.Steps = 100 / progress.Total

	// Check if DAOS Management Service is up and running
	params := support.Params{}
	params.Config = cmd.cfgCmd.config.Path
	params.LogFunction = support.CollectDmgCmdEnum
	params.LogCmd = "dmg system query"

	err := support.CollectSupportLog(cmd.Logger, params)

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
			cmd.Debugf("Log Function %d -- Log Collect Cmd %s ", logfunc, logcmd)
			ctx := context.Background()
			req := &control.CollectLogReq{
				TargetFolder: cmd.TargetFolder,
				CustomLogs:   cmd.CustomLogs,
				LogFunction:  logfunc,
				LogCmd:       logcmd,
			}
			req.SetHostList(cmd.hostlist)
			resp, err := control.CollectLog(ctx, cmd.ctlInvoker, req)
			if err != nil && cmd.Stop {
				return err
			}
			if len(resp.GetHostErrors()) > 0 {
				var bld strings.Builder
				if err := pretty.PrintResponseErrors(resp, &bld); err != nil {
					return err
				}
				cmd.Info(bld.String())
				if cmd.Stop {
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
		LogFunction:  support.RsyncLogEnum,
		LogCmd:       hostName,
	}
	cmd.Debugf("Rsync logs from servers to %s:%s ", hostName, cmd.TargetFolder)
	resp, err := control.CollectLog(context.Background(), cmd.ctlInvoker, req)
	if err != nil && cmd.Stop {
		return err
	}
	if len(resp.GetHostErrors()) > 0 {
		var bld strings.Builder
		if err := pretty.PrintResponseErrors(resp, &bld); err != nil {
			return err
		}
		cmd.Info(bld.String())
		if cmd.Stop {
			return resp.Errors()
		}
	}
	fmt.Printf(support.PrintProgress(&progress))

	// Collect dmg command output on Admin node
	var DmgInfoCollection = map[int32][]string{
		support.CollectDmgCmdEnum:      support.DmgCmd,
		support.CollectDmgDiskInfoEnum: {""},
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
				if cmd.Stop {
					return err
				}
			}
		}
		fmt.Printf(support.PrintProgress(&progress))
	}

	// Archive the logs
	if cmd.Archive {
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

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(nil, err)
	}

	return nil
}
