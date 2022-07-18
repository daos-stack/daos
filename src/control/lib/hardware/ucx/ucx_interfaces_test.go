//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build ucx
// +build ucx

package ucx

import (
	"context"
	"fmt"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestUCX_Provider_GetFabricInterfaces_Integrated(t *testing.T) {
	cleanup, err := Load()
	if err != nil {
		t.Errorf("can't load lib (%s)", err.Error())
	}
	defer cleanup()

	// Can't mock the underlying UCX calls, but we can make sure it doesn't crash or
	// error on the normal happy path.

	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	p := NewProvider(log)

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	result, err := p.GetFabricInterfaces(ctx)

	if err != nil {
		t.Fatal(err.Error())
	}

	fmt.Printf("FabricInterfaceSet:\n%s\n", result)
}
