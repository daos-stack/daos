//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
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
