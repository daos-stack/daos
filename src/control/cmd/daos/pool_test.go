//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestDaos_Pool_Autotest(t *testing.T) {
	baseArgs := mkArgs("pool", "autotest")

	for name, tc := range map[string]struct {
		args   []string
		expErr error
	}{
		"no pool ID": {
			expErr: errors.New("no pool UUID or label"),
		},
		"pool arg": {
			args: mkArgs("testpool"),
		},
		"pool flag (deprecated)": {
			args: mkArgs("--pool", "testpool"),
		},
		"skip-big (long)": {
			args: mkArgs("testpool", "--skip-big"),
		},
		"skip-big (short)": {
			args: mkArgs("testpool", "-S"),
		},
		"deadline (long)": {
			args: mkArgs("testpool", "--deadline-limit", "60"),
		},
		"deadline (short)": {
			args: mkArgs("testpool", "-D", "60"),
		},
		"deadline (invalid)": {
			args:   mkArgs("testpool", "-D", "never"),
			expErr: errors.New("invalid argument"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			_, gotErr := runTestCmd(t, log, append(baseArgs, tc.args...)...)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func TestDaos_Pool_GetAttr(t *testing.T) {
	baseArgs := mkArgs("pool", "get-attr")

	for name, tc := range map[string]struct {
		args   []string
		expErr error
	}{
		"pool, attribute": {
			args: mkArgs("test-pool", "test-0"),
		},
		"pool, attribute, extra": {
			args:   mkArgs("test-pool", "test-0", "extra"),
			expErr: errors.New("unexpected arguments"),
		},
		"unknown attribute": {
			args:   mkArgs("test-pool", "unknown-attr"),
			expErr: daos.Nonexistent,
		},
		"no pool ID": {
			args:   mkArgs("attr"),
			expErr: errors.New("pool ID and attribute name are required"),
		},
		"no pool ID with attr flag (deprecated)": {
			args:   mkArgs("--attr", "attr"),
			expErr: errors.New("--attr requires --pool"),
		},
		"pool flag (deprecated), attribute flag (deprecated)": {
			args: mkArgs("--pool", "test-pool", "--attr", "test-0"),
		},
		"pool flag (deprecated), attribute": {
			args:   mkArgs("--pool", "test-pool", "test-0"),
			expErr: errors.New("--attr is required when --pool is specified"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			_, gotErr := runTestCmd(t, log, append(baseArgs, tc.args...)...)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func TestDaos_Pool_DelAttr(t *testing.T) {
	baseArgs := mkArgs("pool", "del-attr")

	for name, tc := range map[string]struct {
		args   []string
		expErr error
	}{
		"pool, attribute": {
			args: mkArgs("test-pool", "test-0"),
		},
		"pool, attribute, extra": {
			args:   mkArgs("test-pool", "test-0", "extra"),
			expErr: errors.New("unexpected arguments"),
		},
		"unknown attribute": {
			args:   mkArgs("test-pool", "unknown-attr"),
			expErr: daos.Nonexistent,
		},
		"no pool ID": {
			args:   mkArgs("attr"),
			expErr: errors.New("pool ID and attribute name are required"),
		},
		"no pool ID with attr flag (deprecated)": {
			args:   mkArgs("--attr", "attr"),
			expErr: errors.New("--attr requires --pool"),
		},
		"pool flag (deprecated), attribute flag (deprecated)": {
			args: mkArgs("--pool", "test-pool", "--attr", "test-0"),
		},
		"pool flag (deprecated), attribute": {
			args:   mkArgs("--pool", "test-pool", "test-0"),
			expErr: errors.New("--attr is required when --pool is specified"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			_, gotErr := runTestCmd(t, log, append(baseArgs, tc.args...)...)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func TestDaos_Pool_ListAttr(t *testing.T) {
	baseArgs := mkArgs("pool", "list-attr")

	for name, tc := range map[string]struct {
		args   []string
		expErr error
	}{
		"pool": {
			args: mkArgs("test-pool"),
		},
		"pool, extra": {
			args:   mkArgs("test-pool", "extra"),
			expErr: errors.New("unexpected arguments"),
		},
		"no pool ID": {
			expErr: errors.New("pool ID is required"),
		},
		"pool flag (deprecated)": {
			args: mkArgs("--pool", "test-pool"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			_, gotErr := runTestCmd(t, log, append(baseArgs, tc.args...)...)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func TestDaos_Pool_SetAttr(t *testing.T) {
	baseArgs := mkArgs("pool", "set-attr")

	for name, tc := range map[string]struct {
		args   []string
		expErr error
	}{
		"pool, attribute, value": {
			args: mkArgs("test-pool", "test-0", "test-val-0"),
		},
		"pool, attribute, value, extra": {
			args:   mkArgs("test-pool", "test-0", "test-val-0", "extra"),
			expErr: errors.New("unexpected arguments"),
		},
		"no pool ID": {
			args:   mkArgs("attr", "test-val"),
			expErr: errors.New("attribute name and value are required"),
		},
		"pool flag (deprecated), attribute flag (deprecated), value flag (deprecated)": {
			args: mkArgs("--pool", "test-pool", "--attr", "test-0", "--value", "test-val-0"),
		},
		"pool flag (deprecated), attribute flag (deprecated), value": {
			args:   mkArgs("--pool", "test-pool", "--attr", "test-0", "test-val-0"),
			expErr: errors.New("--attr requires --value"),
		},
		"pool flag (deprecated), attribute, value": {
			args:   mkArgs("--pool", "test-pool", "test-cont", "test-0", "test-val-0"),
			expErr: errors.New("--attr is required when --pool is specified"),
		},
		"pool with attr flag (deprecated) and value flag (deprecated)": {
			args:   mkArgs("test-pool", "--attr", "test-0", "--value", "test-val-0"),
			expErr: errors.New("--attr requires --pool"),
		},
		"pool, attribute with value flag (deprecated)": {
			args:   mkArgs("test-pool", "test-0", "--value", "test-val-0"),
			expErr: errors.New("--value requires --attr"),
		},
		"pool, attribute flag (deprecated), value": {
			args:   mkArgs("test-pool", "--attr", "test-0", "test-val-0"),
			expErr: errors.New("--attr requires --value"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			_, gotErr := runTestCmd(t, log, append(baseArgs, tc.args...)...)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func TestDaos_Pool_Query(t *testing.T) {
	baseArgs := mkArgs("pool", "query")

	for name, tc := range map[string]struct {
		args   []string
		expErr error
	}{
		"pool arg": {
			args: mkArgs("test-pool"),
		},
		"pool, extra": {
			args:   mkArgs("test-pool", "extra"),
			expErr: errors.New("unexpected arguments"),
		},
		"no pool ID": {
			expErr: errors.New("pool ID is required"),
		},
		"pool flag (deprecated)": {
			args: mkArgs("--pool", "test-pool"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			_, gotErr := runTestCmd(t, log, append(baseArgs, tc.args...)...)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}
