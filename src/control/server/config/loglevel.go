//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package config

import "github.com/daos-stack/daos/src/control/logging"

// ControlLogLevel is a type that specifies log levels
type ControlLogLevel logging.LogLevel

// TODO(mjmac): Evaluate whether or not this layer of indirection
// adds any value.
const (
	ControlLogLevelDebug = ControlLogLevel(logging.LogLevelDebug)
	ControlLogLevelInfo  = ControlLogLevel(logging.LogLevelInfo)
	ControlLogLevelError = ControlLogLevel(logging.LogLevelError)
)

// UnmarshalYAML implements yaml.Unmarshaler on ControlLogMask struct
func (c *ControlLogLevel) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var strLevel string
	if err := unmarshal(&strLevel); err != nil {
		return err
	}

	var level logging.LogLevel
	if err := level.SetString(strLevel); err != nil {
		return err
	}
	*c = ControlLogLevel(level)
	return nil
}

func (c ControlLogLevel) MarshalYAML() (interface{}, error) {
	return c.String(), nil
}

func (c ControlLogLevel) String() string {
	return logging.LogLevel(c).String()
}
