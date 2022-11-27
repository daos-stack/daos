//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"testing"

	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestBadCommand(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	var opts mainOpts
	err := parseOpts([]string{"foo"}, &opts, log)
	testExpectedError(t, fmt.Errorf("Unknown command `foo'"), err)
}

func TestNoCommand(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	var opts mainOpts
	err := parseOpts([]string{}, &opts, log)
	testExpectedError(t, fmt.Errorf("Please specify one command"), err)
}

func TestPreExecCheckBypass(t *testing.T) {
	for name, tc := range map[string]struct {
		cmdLine string
		expErr  error
	}{
		"help": {
			cmdLine: "--help",
			expErr:  &flags.Error{Type: flags.ErrHelp},
		},
		"version": {
			cmdLine: "version",
		},
		"start (should fail)": {
			cmdLine: "start",
			expErr:  errors.New("ouch"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			opts := mainOpts{
				preExecTests: []execTestFn{
					func() error {
						return errors.New("ouch")
					},
				},
			}
			err := parseOpts([]string{tc.cmdLine}, &opts, log)
			test.CmpErr(t, tc.expErr, err)
		})
	}
}
