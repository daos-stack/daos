//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build firmware
// +build firmware

package main

import (
	"os"

	"github.com/daos-stack/daos/src/control/pbin"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func main() {
	app := pbin.NewApp().WithAllowedCallers("daos_server")

	if logPath, set := os.LookupEnv(pbin.DaosFWLogFileEnvVar); set {
		app = app.WithLogFile(logPath)
	}

	app.AddHandler(storage.ScmFirmwareQueryMethod, &scmQueryHandler{})
	app.AddHandler(storage.ScmFirmwareUpdateMethod, &scmUpdateHandler{})
	app.AddHandler(storage.NVMeFirmwareQueryMethod, &nvmeQueryHandler{})
	app.AddHandler(storage.NVMeFirmwareUpdateMethod, &nvmeUpdateHandler{})

	err := app.Run()
	if err != nil {
		os.Exit(1)
	}
}
