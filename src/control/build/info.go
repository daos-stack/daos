//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build go1.18

package build

import (
	"runtime/debug"
	"strings"
	"time"
)

func init() {
	info, ok := debug.ReadBuildInfo()
	if !ok {
		return
	}

	for _, setting := range info.Settings {
		switch setting.Key {
		case "vcs":
			VCS = setting.Value
		case "vcs.revision":
			Revision = setting.Value
		case "vcs.modified":
			DirtyBuild = setting.Value == "true"
		case "vcs.time":
			LastCommit, _ = time.Parse(time.RFC3339, setting.Value)
		case "-tags":
			if strings.Contains(setting.Value, "release") {
				ReleaseBuild = true
			}
		}
	}
}
