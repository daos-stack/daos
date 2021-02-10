//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

// +build firmware

package main

import (
	"os"

	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

func main() {
	app := pbin.NewApp().WithAllowedCallers("daos_server")

	if logPath, set := os.LookupEnv(pbin.DaosFWLogFileEnvVar); set {
		app = app.WithLogFile(logPath)
	}

	app.AddHandler(scm.FirmwareQueryMethod, &scmQueryHandler{})
	app.AddHandler(scm.FirmwareUpdateMethod, &scmUpdateHandler{})
	app.AddHandler(bdev.FirmwareUpdateMethod, &nvmeUpdateHandler{})

	err := app.Run()
	if err != nil {
		os.Exit(1)
	}
}
