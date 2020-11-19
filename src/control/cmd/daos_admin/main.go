//
// (C) Copyright 2019-2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package main

import (
	"os"

	"github.com/daos-stack/daos/src/control/pbin"
)

func main() {
	app := pbin.NewApp().
		WithAllowedCallers("daos_server")

	if logPath, set := os.LookupEnv(pbin.DaosAdminLogFileEnvVar); set {
		app = app.WithLogFile(logPath)
	}

	addMethodHandlers(app)

	err := app.Run()
	if err != nil {
		os.Exit(1)
	}
}

// addMethodHandlers adds all of daos_admin's supported handler functions.
func addMethodHandlers(app *pbin.App) {
	app.AddHandler("ScmMount", &scmMountUnmountHandler{})
	app.AddHandler("ScmUnmount", &scmMountUnmountHandler{})
	app.AddHandler("ScmFormat", &scmFormatCheckHandler{})
	app.AddHandler("ScmCheckFormat", &scmFormatCheckHandler{})
	app.AddHandler("ScmScan", &scmScanHandler{})
	app.AddHandler("ScmPrepare", &scmPrepHandler{})

	app.AddHandler("BdevPrepare", &bdevPrepHandler{})
	app.AddHandler("BdevScan", &bdevScanHandler{})
	app.AddHandler("BdevFormat", &bdevFormatHandler{})
}
