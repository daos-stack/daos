//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

//go:build test_stubs

// cgo drivers for server_test.go (see test_helpers.go for why these can't
// live in the test file).

package main

/*
#include <stdint.h>
*/
import "C"
import "runtime/cgo"

func callServerSetLogmasks(handle cgo.Handle, host, masks, streams, subsystems string) int {
	cHost, hF := cString(host)
	defer hF()
	cMasks, mF := cString(masks)
	defer mF()
	cStreams, strF := cString(streams)
	defer strF()
	cSubs, sF := cString(subsystems)
	defer sF()
	return int(daos_control_server_set_logmasks(C.uintptr_t(handle), cHost, cMasks, cStreams, cSubs))
}
