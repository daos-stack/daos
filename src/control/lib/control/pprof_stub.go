//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build !pprof
// +build !pprof

package control

import "github.com/daos-stack/daos/src/control/logging"

func StartPProf(log logging.Logger) {
	log.Debug("profiling is disabled for this build")
}
