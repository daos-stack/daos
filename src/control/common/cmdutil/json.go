//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package cmdutil

import (
	"encoding/json"
	"io"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/daos"
)

var _ JSONOutputter = (*JSONOutputCmd)(nil)

type (
	// JSONOutputter is an interface for commands that can output JSON.
	JSONOutputter interface {
		EnableJSONOutput(io.Writer, *atm.Bool)
		JSONOutputEnabled() bool
		OutputJSON(interface{}, error) error
	}
)

// OutputJSON writes the given data or error to the given writer as JSON.
func OutputJSON(writer io.Writer, in interface{}, inErr error) error {
	status := 0
	var errStr *string
	if inErr != nil {
		errStr = func() *string { str := inErr.Error(); return &str }()
		if s, ok := errors.Cause(inErr).(daos.Status); ok {
			status = int(s)
		} else {
			status = int(daos.MiscError)
		}
	}

	data, err := json.MarshalIndent(struct {
		Response interface{} `json:"response"`
		Error    *string     `json:"error"`
		Status   int         `json:"status"`
	}{in, errStr, status}, "", "  ")
	if err != nil {
		return err
	}

	if _, err = writer.Write(append(data, []byte("\n")...)); err != nil {
		return err
	}

	return inErr
}

// JSONOutputCmd is a struct that implements JSONOutputter and
// can be embedded in a command struct to provide JSON output.
type JSONOutputCmd struct {
	writer      io.Writer
	jsonEnabled atm.Bool
	wroteJSON   *atm.Bool
}

// EnableJSONOutput enables JSON output to the given writer. The
// wroteJSON parameter is optional and is used to track whether
// JSON has been written to the writer.
func (cmd *JSONOutputCmd) EnableJSONOutput(writer io.Writer, wroteJSON *atm.Bool) {
	cmd.wroteJSON = wroteJSON
	if cmd.wroteJSON == nil {
		cmd.wroteJSON = atm.NewBoolRef(false)
	}
	cmd.writer = writer
	cmd.jsonEnabled.SetTrue()
}

// JSONOutputEnabled returns true if JSON output is enabled.
func (cmd *JSONOutputCmd) JSONOutputEnabled() bool {
	return cmd.jsonEnabled.IsTrue()
}

// OutputJSON writes the given data or error to the command's writer as JSON.
func (cmd *JSONOutputCmd) OutputJSON(in interface{}, err error) error {
	if cmd.JSONOutputEnabled() && cmd.wroteJSON.IsFalse() {
		cmd.wroteJSON.SetTrue()
		return OutputJSON(cmd.writer, in, err)
	}

	return nil
}
