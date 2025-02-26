//
// (C) Copyright 2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build !test_stubs
// +build !test_stubs

package main

import "github.com/daos-stack/daos/src/control/lib/daos/api"

var (
	RunSelfTest   = api.RunSelfTest
	GetPoolList   = api.GetPoolList
	PoolConnect   = api.PoolConnect
	ContainerOpen = api.ContainerOpen
)
