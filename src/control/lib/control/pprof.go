//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build pprof
// +build pprof

package control

import (
	"net/http"
	_ "net/http/pprof"

	"github.com/daos-stack/daos/src/control/logging"
)

const (
	DefaultPProfPort = "6060"
)

// StartPProf starts the profiling server.
func StartPProf(log logging.Logger) {
	go func() {
		http.ListenAndServe(":"+DefaultPProfPort, nil)
	}()

	log.Debugf("profiling service started on port %s", DefaultPProfPort)
}
