//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build test_stubs
// +build test_stubs

package api

func reset_ioctl_stubs() {
	reset_call_dfuse_telemetry_ioctl()
}

var (
	call_dfuse_telemetry_ioctl_Error   error
	call_dfuse_telemetry_ioctl_Enabled bool
)

func reset_call_dfuse_telemetry_ioctl() {
	call_dfuse_telemetry_ioctl_Error = nil
	call_dfuse_telemetry_ioctl_Enabled = false
}

func call_dfuse_telemetry_ioctl(_ string, enabled bool) error {
	if call_dfuse_telemetry_ioctl_Error != nil {
		return call_dfuse_telemetry_ioctl_Error
	}

	call_dfuse_telemetry_ioctl_Enabled = enabled
	return nil
}
