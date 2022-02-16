//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build fault_injection
// +build fault_injection

package checker

import (
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

func NewInjectedFinding(f *mgmtpb.SystemCheckerFinding) *Finding {
	var class FindingClass
	if err := class.FromString(f.Class); err != nil {
		panic(err)
	}

	return &Finding{
		ID:          f.Id,
		Class:       class,
		Status:      FindingStatus(f.Status),
		Ignorable:   f.Ignorable,
		Description: f.Description,
	}
}
