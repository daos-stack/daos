package main

import (
	"fmt"
	"testing"
)

func TestServiceCommands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		// FIXME: kill-rank should probably require these arguments
		/*{
			"Kill rank with missing arguments",
			"service kill-rank",
			"ConnectClients KillRank",
			nil,
			errMissingFlag,
		},*/
		{
			"Kill rank",
			"service kill-rank --pool-uuid 031bcaf8-f0f5-42ef-b3c5-ee048676dceb --rank 2",
			"ConnectClients KillRank-uuid 031bcaf8-f0f5-42ef-b3c5-ee048676dceb, rank 2",
			nil,
			cmdSuccess,
		},
		{
			"Nonexistent subcommand",
			"service quack",
			"",
			nil,
			fmt.Errorf("Unknown command"),
		},
	})
}
