//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
// +build !firmware

package main

// firmwareOption is not available in builds without firmware management enabled.
type firmwareOption struct{}
