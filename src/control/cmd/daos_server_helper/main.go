//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"os"

	"github.com/daos-stack/daos/src/control/pbin"
)

func main() {
	app := pbin.NewApp().
		WithAllowedCallers("daos_server")

	if logPath, set := os.LookupEnv(pbin.DaosPrivHelperLogFileEnvVar); set {
		app = app.WithLogFile(logPath)
	}

	addMethodHandlers(app)

	err := app.Run()
	if err != nil {
		os.Exit(1)
	}
}

// addMethodHandlers adds all of daos_server_helper's supported handler functions.
func addMethodHandlers(app *pbin.App) {
	app.AddHandler("MetadataMount", &metadataMountHandler{})
	app.AddHandler("MetadataUnmount", &metadataMountHandler{})
	app.AddHandler("MetadataFormat", &metadataFormatHandler{})
	app.AddHandler("MetadataNeedsFormat", &metadataFormatHandler{})

	app.AddHandler("ScmMount", &scmMountUnmountHandler{})
	app.AddHandler("ScmUnmount", &scmMountUnmountHandler{})
	app.AddHandler("ScmFormat", &scmFormatCheckHandler{})
	app.AddHandler("ScmCheckFormat", &scmFormatCheckHandler{})
	app.AddHandler("ScmScan", &scmScanHandler{})
	app.AddHandler("ScmPrepare", &scmPrepHandler{})

	app.AddHandler("BdevPrepare", &bdevPrepHandler{})
	app.AddHandler("BdevScan", &bdevScanHandler{})
	app.AddHandler("BdevFormat", &bdevFormatHandler{})
	app.AddHandler("BdevWriteConfig", &bdevWriteConfigHandler{})
}
