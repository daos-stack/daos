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

func TestDaos_Container_Create(t *testing.T) {
	baseArgs := mkArgs("container", "create")

	for name, tc := range map[string]struct {
		args   []string
		expErr error
	}{
		"no pool ID or dfs path": {
			expErr: errors.New("no pool ID or dfs path"),
		},
		"namespace path (long)": {
			args: mkArgs("--path", "/tmp/dfs"),
		},
		"namespace path (short)": {
			args: mkArgs("-d", "/tmp/dfs"),
		},
		"namespace path with pool": {
			args: mkArgs("-d", "/tmp/dfs", "testpool"),
		},
		"namespace path with pool flag (deprecated)": {
			args: mkArgs("-d", "/tmp/dfs", "--pool", "testpool"),
		},
		"pool UUID": {
			args: mkArgs(test.MockUUID()),
		},
		"user (long)": {
			args: mkArgs("--user", "testuser", "testpool"),
		},
		"user (short)": {
			args: mkArgs("-u", "testuser", "testpool"),
		},
		"group (long)": {
			args: mkArgs("--group", "testgroup", "testpool"),
		},
		"group (short)": {
			args: mkArgs("-g", "testgroup", "testpool"),
		},
		"ACL (long)": {
			args: mkArgs("--acl-file", "testfile", "testpool"),
		},
		"ACL (short)": {
			args: mkArgs("-A", "testfile", "testpool"),
		},
		"DFS consistency mode (long)": {
			args: mkArgs("--mode", "relaxed", "testpool"),
		},
		"DFS consistency mode (short)": {
			args: mkArgs("-M", "balanced", "testpool"),
		},
		"DFS consistency mode (unknown)": {
			args:   mkArgs("-M", "unknown", "testpool"),
			expErr: errors.New("unknown consistency mode"),
		},
		"container type (long)": {
			args: mkArgs("--type", "POSIX", "testpool"),
		},
		"container type (short)": {
			args: mkArgs("-t", "HDF5", "testpool"),
		},
		"container type (unknown)": {
			args:   mkArgs("-t", "unknown", "testpool"),
			expErr: errors.New("unknown container type"),
		},
		"chunk size (long)": {
			args: mkArgs("--chunk-size", "1M", "testpool"),
		},
		"chunk size (short)": {
			args: mkArgs("-z", "1M", "testpool"),
		},
		"chunk size (invalid)": {
			args:   mkArgs("-z", "quack", "testpool"),
			expErr: errors.New("invalid chunk size"),
		},
		"object class (long)": {
			args: mkArgs("--oclass", "S1", "testpool"),
		},
		"object class (short)": {
			args: mkArgs("-o", "S2", "testpool"),
		},
		"object class (invalid)": {
			args:   mkArgs("-o", "quack", "testpool"),
			expErr: errors.New("unknown object class"),
		},
		"label (long)": {
			args: mkArgs("--label", "testlabel", "testpool"),
		},
		"label (short)": {
			args: mkArgs("-l", "testlabel", "testpool"),
		},
		"label with property": {
			args:   mkArgs("-l", "testlabel", "--properties", "label:foo", "testpool"),
			expErr: errors.New("can't use both --label and --properties label:"),
		},
		"bad pool label": {
			args:   mkArgs("this!is!a!bad!pool!label"),
			expErr: errors.New("invalid label"),
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

func TestDaos_Container_Query(t *testing.T) {
	baseArgs := mkArgs("container", "query")

	for name, tc := range map[string]struct {
		args   []string
		expErr error
	}{
		"pool, container": {
			args: mkArgs("test-pool", "test-cont"),
		},
		"pool, container-uuid": {
			args: mkArgs("test-pool", test.MockUUID()),
		},
		"pool, container, extra": {
			args:   mkArgs("test-pool", "test-cont", "extra"),
			expErr: errors.New("unexpected arguments"),
		},
		"no pool/cont IDs or dfs path": {
			args:   mkArgs("attr"),
			expErr: errors.New("pool and container IDs must be specified"),
		},
		"namespace path (long)": {
			args: mkArgs("--path", "/tmp/dfs"),
		},
		"namespace path (short)": {
			args: mkArgs("-d", "/tmp/dfs"),
		},
		"namespace path with pool and container": {
			args:   mkArgs("--path", "/tmp/dfs", "test-pool", "test-cont"),
			expErr: errors.New("unexpected arguments"),
		},
		"pool flag (deprecated), container flag (deprecated)": {
			args: mkArgs("--pool", "test-pool", "--cont", "test-cont"),
		},
		"pool, container flag (deprecated)": {
			args:   mkArgs("test-pool", "--cont", "test-cont"),
			expErr: errors.New("--cont flag requires --pool"),
		},
		"pool flag (deprecated), container": {
			args:   mkArgs("--pool", "test-pool", "test-cont"),
			expErr: errors.New("--pool flag requires --cont"),
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

func TestDaos_Container_Destroy(t *testing.T) {
	baseArgs := mkArgs("container", "destroy")

	for name, tc := range map[string]struct {
		args   []string
		expErr error
	}{
		"pool, container": {
			args: mkArgs("test-pool", "test-cont"),
		},
		"pool, container-uuid": {
			args: mkArgs("test-pool", test.MockUUID()),
		},
		"pool, container, extra": {
			args:   mkArgs("test-pool", "test-cont", "extra"),
			expErr: errors.New("unexpected arguments"),
		},
		"no pool/cont IDs or dfs path": {
			args:   mkArgs("attr"),
			expErr: errors.New("pool and container IDs must be specified"),
		},
		"namespace path (long)": {
			args: mkArgs("--path", "/tmp/dfs"),
		},
		"namespace path (short)": {
			args: mkArgs("-d", "/tmp/dfs"),
		},
		"namespace path with pool and container": {
			args:   mkArgs("--path", "/tmp/dfs", "test-pool", "test-cont"),
			expErr: errors.New("unexpected arguments"),
		},
		"pool flag (deprecated), container flag (deprecated)": {
			args: mkArgs("--pool", "test-pool", "--cont", "test-cont"),
		},
		"pool, container flag (deprecated)": {
			args:   mkArgs("test-pool", "--cont", "test-cont"),
			expErr: errors.New("--cont flag requires --pool"),
		},
		"pool flag (deprecated), container": {
			args:   mkArgs("--pool", "test-pool", "test-cont"),
			expErr: errors.New("--pool flag requires --cont"),
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

func TestDaos_Container_GetAttr(t *testing.T) {
	baseArgs := mkArgs("container", "get-attr")

	for name, tc := range map[string]struct {
		args   []string
		expErr error
	}{
		"pool, container, attribute": {
			args: mkArgs("test-pool", "test-cont", "test-0"),
		},
		"pool, container-uuid, attribute": {
			args: mkArgs("test-pool", test.MockUUID(), "test-0"),
		},
		"pool, container, attribute, extra": {
			args:   mkArgs("test-pool", "test-cont", "test-0", "extra"),
			expErr: errors.New("unexpected arguments"),
		},
		"unknown attribute": {
			args:   mkArgs("test-pool", "test-cont", "unknown-attr"),
			expErr: daos.Nonexistent,
		},
		"no pool/cont IDs or dfs path": {
			args:   mkArgs("attr"),
			expErr: errors.New("pool and container IDs must be specified"),
		},
		"namespace path (long)": {
			args: mkArgs("--path", "/tmp/dfs", "test-0"),
		},
		"namespace path (short)": {
			args: mkArgs("-d", "/tmp/dfs", "test-0"),
		},
		"namespace path with pool flag (deprecated) and container flag (deprecated)": {
			args:   mkArgs("--path", "/tmp/dfs", "--pool", "test-pool", "--cont", "test-cont", "--attr", "test-0"),
			expErr: errors.New("--path flag may not be set"),
		},
		"namespace path with attr flag (deprecated)": {
			args: mkArgs("-d", "/tmp/dfs", "--attr", "test-0"),
		},
		"pool flag (deprecated), container flag (deprecated), attribute flag (deprecated)": {
			args: mkArgs("--pool", "test-pool", "--cont", "test-cont", "--attr", "test-0"),
		},
		"pool, container, attribute with attr flag (deprecated)": {
			args:   mkArgs("test-pool", "test-cont", "--attr", "test-0"),
			expErr: errors.New("--attr requires both --pool and --cont"),
		},
		"pool flag (deprecated), container flag (deprecated), attribute": {
			args:   mkArgs("--pool", "test-pool", "--cont", "test-cont", "test-0"),
			expErr: errors.New("--attr is required when --pool and --cont are specified"),
		},
		"pool flag (deprecated), container, attribute": {
			args:   mkArgs("--pool", "test-pool", "test-cont", "test-0"),
			expErr: errors.New("--attr is required when --pool and --cont are specified"),
		},
		"pool, container flag (deprecated), attribute": {
			args:   mkArgs("test-pool", "--cont", "test-cont", "test-0"),
			expErr: errors.New("--attr is required when --pool and --cont are specified"),
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

func TestDaos_Container_DelAttr(t *testing.T) {
	baseArgs := mkArgs("container", "del-attr")

	for name, tc := range map[string]struct {
		args   []string
		expErr error
	}{
		"pool, container, attribute": {
			args: mkArgs("test-pool", "test-cont", "test-0"),
		},
		"pool, container-uuid, attribute": {
			args: mkArgs("test-pool", test.MockUUID(), "test-0"),
		},
		"pool, container, attribute, extra": {
			args:   mkArgs("test-pool", "test-cont", "test-0", "extra"),
			expErr: errors.New("unexpected arguments"),
		},
		"unknown attribute": {
			args:   mkArgs("test-pool", "test-cont", "unknown-attr"),
			expErr: daos.Nonexistent,
		},
		"no pool/cont IDs or dfs path": {
			args:   mkArgs("attr"),
			expErr: errors.New("pool and container IDs must be specified"),
		},
		"namespace path (long)": {
			args: mkArgs("--path", "/tmp/dfs", "test-0"),
		},
		"namespace path (short)": {
			args: mkArgs("-d", "/tmp/dfs", "test-0"),
		},
		"namespace path with pool flag (deprecated) and container flag (deprecated)": {
			args:   mkArgs("--path", "/tmp/dfs", "--pool", "test-pool", "--cont", "test-cont", "--attr", "test-0"),
			expErr: errors.New("--path flag may not be set"),
		},
		"namespace path with attr flag (deprecated)": {
			args: mkArgs("-d", "/tmp/dfs", "--attr", "test-0"),
		},
		"pool flag (deprecated), container flag (deprecated), attribute flag (deprecated)": {
			args: mkArgs("--pool", "test-pool", "--cont", "test-cont", "--attr", "test-0"),
		},
		"pool, container, attribute with attr flag (deprecated)": {
			args:   mkArgs("test-pool", "test-cont", "--attr", "test-0"),
			expErr: errors.New("--attr requires both --pool and --cont"),
		},
		"pool flag (deprecated), container flag (deprecated), attribute": {
			args:   mkArgs("--pool", "test-pool", "--cont", "test-cont", "test-0"),
			expErr: errors.New("--attr is required when --pool and --cont are specified"),
		},
		"pool flag (deprecated), container, attribute": {
			args:   mkArgs("--pool", "test-pool", "test-cont", "test-0"),
			expErr: errors.New("--attr is required when --pool and --cont are specified"),
		},
		"pool, container flag (deprecated), attribute": {
			args:   mkArgs("test-pool", "--cont", "test-cont", "test-0"),
			expErr: errors.New("--attr is required when --pool and --cont are specified"),
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

func TestDaos_Container_ListAttr(t *testing.T) {
	baseArgs := mkArgs("container", "list-attr")

	for name, tc := range map[string]struct {
		args   []string
		expErr error
	}{
		"pool, container": {
			args: mkArgs("test-pool", "test-cont"),
		},
		"pool, container-uuid": {
			args: mkArgs("test-pool", test.MockUUID()),
		},
		"pool, container, extra": {
			args:   mkArgs("test-pool", "test-cont", "extra"),
			expErr: errors.New("unexpected arguments"),
		},
		"no pool/cont IDs or dfs path": {
			args:   mkArgs("attr"),
			expErr: errors.New("pool and container IDs must be specified"),
		},
		"namespace path (long)": {
			args: mkArgs("--path", "/tmp/dfs"),
		},
		"namespace path (short)": {
			args: mkArgs("-d", "/tmp/dfs"),
		},
		"namespace path with pool and container": {
			args:   mkArgs("--path", "/tmp/dfs", "test-pool", "test-cont"),
			expErr: errors.New("unexpected arguments"),
		},
		"pool flag (deprecated), container flag (deprecated)": {
			args: mkArgs("--pool", "test-pool", "--cont", "test-cont"),
		},
		"pool, container flag (deprecated)": {
			args:   mkArgs("test-pool", "--cont", "test-cont"),
			expErr: errors.New("--cont flag requires --pool"),
		},
		"pool flag (deprecated), container": {
			args:   mkArgs("--pool", "test-pool", "test-cont"),
			expErr: errors.New("--pool flag requires --cont"),
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

func TestDaos_Container_SetAttr(t *testing.T) {
	baseArgs := mkArgs("container", "set-attr")

	for name, tc := range map[string]struct {
		args   []string
		expErr error
	}{
		"pool, container, attribute, value": {
			args: mkArgs("test-pool", "test-cont", "test-0", "test-val-0"),
		},
		"pool, container-uuid, attribute, value": {
			args: mkArgs("test-pool", test.MockUUID(), "test-0", "test-val-0"),
		},
		"pool, container, attribute, value, extra": {
			args:   mkArgs("test-pool", "test-cont", "test-0", "test-val-0", "extra"),
			expErr: errors.New("unexpected arguments"),
		},
		"no pool/cont IDs or dfs path": {
			args:   mkArgs("attr", "test-val"),
			expErr: errors.New("attribute name and value are required"),
		},
		"namespace path (long)": {
			args: mkArgs("--path", "/tmp/dfs", "test-0", "test-val-0"),
		},
		"namespace path (short)": {
			args: mkArgs("-d", "/tmp/dfs", "test-0", "test-val-0"),
		},
		"namespace path with pool flag (deprecated) and container flag (deprecated)": {
			args:   mkArgs("--path", "/tmp/dfs", "--pool", "test-pool", "--cont", "test-cont", "--attr", "test-0", "--value", "test-val-0"),
			expErr: errors.New("--path flag may not be set"),
		},
		"namespace path with attr flag (deprecated) and value flag (deprecated)": {
			args: mkArgs("-d", "/tmp/dfs", "--attr", "test-0", "--value", "test-val-0"),
		},
		"namespace path with attr flag (deprecated) and value": {
			args:   mkArgs("-d", "/tmp/dfs", "--attr", "test-0", "test-val-0"),
			expErr: errors.New("--attr requires --value"),
		},
		"namespace path with attr and value flag (deprecated)": {
			args:   mkArgs("-d", "/tmp/dfs", "test-0", "--value", "test-val-0"),
			expErr: errors.New("--value requires --attr"),
		},
		"pool flag (deprecated), container flag (deprecated), attribute flag (deprecated), value flag (deprecated)": {
			args: mkArgs("--pool", "test-pool", "--cont", "test-cont", "--attr", "test-0", "--value", "test-val-0"),
		},
		"pool flag (deprecated), container flag (deprecated), attribute flag (deprecated), value": {
			args:   mkArgs("--pool", "test-pool", "--cont", "test-cont", "--attr", "test-0", "test-val-0"),
			expErr: errors.New("--attr requires --value"),
		},
		"pool flag (deprecated), container flag (deprecated), attribute, value": {
			args:   mkArgs("--pool", "test-pool", "--cont", "test-cont", "test-0", "test-val-0"),
			expErr: errors.New("--attr is required when --pool and --cont are specified"),
		},
		"pool flag (deprecated), container, attribute, value": {
			args:   mkArgs("--pool", "test-pool", "test-cont", "test-0", "test-val-0"),
			expErr: errors.New("--attr is required when --pool and --cont are specified"),
		},
		"pool, container with attr flag (deprecated) and value flag (deprecated)": {
			args:   mkArgs("test-pool", "test-cont", "--attr", "test-0", "--value", "test-val-0"),
			expErr: errors.New("--attr requires both --pool and --cont"),
		},
		"pool, container, attribute with value flag (deprecated)": {
			args:   mkArgs("test-pool", "test-cont", "test-0", "--value", "test-val-0"),
			expErr: errors.New("--value requires --attr"),
		},
		"pool, container, attribute flag (deprecated), value": {
			args:   mkArgs("test-pool", "test-cont", "--attr", "test-0", "test-val-0"),
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

func TestDaos_Container_GetProp(t *testing.T) {
	baseArgs := mkArgs("container", "get-prop")
	propKey := propHdlrs.keys()[0]

	for name, tc := range map[string]struct {
		args   []string
		expErr error
	}{
		"pool, container, property": {
			args: mkArgs("test-pool", "test-cont", propKey),
		},
		"pool, container-uuid, property": {
			args: mkArgs("test-pool", test.MockUUID(), propKey),
		},
		"pool, container, property, extra": {
			args:   mkArgs("test-pool", "test-cont", propKey, "extra"),
			expErr: errors.New("unexpected arguments"),
		},
		"pool, container (defaults to all)": {
			args: mkArgs("test-pool", "test-cont"),
		},
		"unknown property": {
			args:   mkArgs("test-pool", "test-cont", "unknown-prop"),
			expErr: errors.New("unknown property"),
		},
		"no pool/cont IDs or dfs path": {
			args:   mkArgs(propKey),
			expErr: errors.New("pool and container IDs must be specified"),
		},
		"namespace path (long), property": {
			args: mkArgs("--path", "/tmp/dfs", propKey),
		},
		"namespace path (short), property": {
			args: mkArgs("-d", "/tmp/dfs", propKey),
		},
		"namespace path (defaults to all)": {
			args: mkArgs("-d", "/tmp/dfs"),
		},
		"namespace path with pool flag (deprecated) and container flag (deprecated)": {
			args:   mkArgs("--path", "/tmp/dfs", "--pool", "test-pool", "--cont", "test-cont"),
			expErr: errors.New("--path flag may not be set"),
		},
		"namespace path with properties flag (deprecated)": {
			args: mkArgs("-d", "/tmp/dfs", "--properties", propKey),
		},
		"pool flag (deprecated), container flag (deprecated) (defaults to all)": {
			args: mkArgs("--pool", "test-pool", "--cont", "test-cont"),
		},
		"pool flag (deprecated), container flag (deprecated), property flag (deprecated)": {
			args: mkArgs("--pool", "test-pool", "--cont", "test-cont", "--properties", propKey),
		},
		"pool, container, properties flag (deprecated)": {
			args:   mkArgs("test-pool", "test-cont", "--properties", propKey),
			expErr: errors.New("--properties requires both --pool and --cont"),
		},
		"pool flag (deprecated), container flag (deprecated), property": {
			args:   mkArgs("--pool", "test-pool", "--cont", "test-cont", propKey),
			expErr: errors.New("--properties is required when --pool and --cont are specified"),
		},
		"pool flag (deprecated), container, property": {
			args:   mkArgs("--pool", "test-pool", "test-cont", propKey),
			expErr: errors.New("--properties is required when --pool and --cont are specified"),
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

func TestDaos_Container_SetProp(t *testing.T) {
	baseArgs := mkArgs("container", "set-prop")
	propKeyVal := "label:foo"

	for name, tc := range map[string]struct {
		args   []string
		expErr error
	}{
		"pool, container, property:val": {
			args: mkArgs("test-pool", "test-cont", propKeyVal),
		},
		"pool, container-uuid, property:val": {
			args: mkArgs("test-pool", test.MockUUID(), propKeyVal),
		},
		"pool, container, property:val, extra": {
			args:   mkArgs("test-pool", "test-cont", propKeyVal, "extra"),
			expErr: errors.New("unexpected arguments"),
		},
		"unknown property": {
			args:   mkArgs("test-pool", "test-cont", "unknown-prop:foo"),
			expErr: errors.New("not a settable property"),
		},
		"no pool/cont IDs or dfs path": {
			args:   mkArgs(propKeyVal),
			expErr: errors.New("pool and container IDs must be specified"),
		},
		"namespace path (long)": {
			args: mkArgs("--path", "/tmp/dfs", propKeyVal),
		},
		"namespace path (short)": {
			args: mkArgs("-d", "/tmp/dfs", propKeyVal),
		},
		"namespace path with pool flag (deprecated) and container flag (deprecated)": {
			args:   mkArgs("--path", "/tmp/dfs", "--pool", "test-pool", "--cont", "test-cont", "--properties", propKeyVal),
			expErr: errors.New("--path flag may not be set"),
		},
		"namespace path with properties flag (deprecated)": {
			args: mkArgs("-d", "/tmp/dfs", "--properties", propKeyVal),
		},
		"pool flag (deprecated), container flag (deprecated), property flag (deprecated)": {
			args: mkArgs("--pool", "test-pool", "--cont", "test-cont", "--properties", propKeyVal),
		},
		"pool, container, properties flag (deprecated)": {
			args:   mkArgs("test-pool", "test-cont", "--properties", propKeyVal),
			expErr: errors.New("--properties requires both --pool and --cont"),
		},
		"pool flag (deprecated), container flag (deprecated), property": {
			args:   mkArgs("--pool", "test-pool", "--cont", "test-cont", propKeyVal),
			expErr: errors.New("--properties is required when --pool and --cont are specified"),
		},
		"pool flag (deprecated), container, property": {
			args:   mkArgs("--pool", "test-pool", "test-cont", propKeyVal),
			expErr: errors.New("--properties is required when --pool and --cont are specified"),
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
