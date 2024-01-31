//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package logging_test

import (
	"strings"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestLogging_ToFromContext(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	ctx, err := logging.ToContext(test.Context(t), log)
	if err != nil {
		t.Fatal(err)
	}
	log2 := logging.FromContext(ctx)
	log2.Info("test")

	if !strings.Contains(buf.String(), "test") {
		t.Fatal("expected test message in log buffer")
	}
}

func TestLogging_FromContext_Unset(t *testing.T) {
	log := logging.FromContext(test.Context(t))
	log.Info("shouldn't panic (noop)")
}

func TestLogging_FromContext_NilCtx(t *testing.T) {
	log := logging.FromContext(nil)
	log.Info("shouldn't panic (noop)")
}

func TestLogging_ToContext_NilCtx(t *testing.T) {
	_, err := logging.ToContext(nil, &logging.LeveledLogger{})
	if err == nil {
		t.Fatal("expected error")
	}
}

func TestLogging_ToContext_NilLogger(t *testing.T) {
	_, err := logging.ToContext(test.Context(t), nil)
	if err == nil {
		t.Fatal("expected error")
	}
}

func TestLogging_ToContext_AlreadySet(t *testing.T) {
	ctx, err := logging.ToContext(test.Context(t), &logging.LeveledLogger{})
	if err != nil {
		t.Fatal(err)
	}
	_, err = logging.ToContext(ctx, &logging.LeveledLogger{})
	if err == nil {
		t.Fatal("expected error")
	}
}
