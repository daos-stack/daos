//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"testing"

	"github.com/pkg/errors"

	. "github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestCommon_ControlLogLevel_String(t *testing.T) {
	for name, tc := range map[string]struct {
		level     ControlLogLevel
		expResult string
	}{
		"error": {
			level:     ControlLogLevelError,
			expResult: logging.LogLevelError.String(),
		},
		"info": {
			level:     ControlLogLevelInfo,
			expResult: logging.LogLevelInfo.String(),
		},
		"debug": {
			level:     ControlLogLevelDebug,
			expResult: logging.LogLevelDebug.String(),
		},
		"unknown": {
			level:     ControlLogLevel(0xffff),
			expResult: "UNKNOWN",
		},
	} {
		t.Run(name, func(t *testing.T) {
			AssertEqual(t, tc.expResult, tc.level.String(), "")
		})
	}
}

func TestCommon_ControlLogLevel_MarshalYAML(t *testing.T) {
	for name, tc := range map[string]struct {
		level     ControlLogLevel
		expResult string
	}{
		"error": {
			level:     ControlLogLevelError,
			expResult: logging.LogLevelError.String(),
		},
		"info": {
			level:     ControlLogLevelInfo,
			expResult: logging.LogLevelInfo.String(),
		},
		"debug": {
			level:     ControlLogLevelDebug,
			expResult: logging.LogLevelDebug.String(),
		},
		"unknown": {
			level:     ControlLogLevel(0xffff),
			expResult: "UNKNOWN",
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := tc.level.MarshalYAML()

			if err != nil {
				t.Fatal(err)
			}

			AssertEqual(t, tc.expResult, result, "")
		})
	}
}

func TestCommon_ControlLogLevel_UnmarshalYAML(t *testing.T) {
	for name, tc := range map[string]struct {
		yamlStr  string
		yamlErr  error
		expLevel ControlLogLevel
		expErr   error
	}{
		"error": {
			yamlStr:  logging.LogLevelError.String(),
			expLevel: ControlLogLevelError,
		},
		"info": {
			yamlStr:  logging.LogLevelInfo.String(),
			expLevel: ControlLogLevelInfo,
		},
		"debug": {
			yamlStr:  logging.LogLevelDebug.String(),
			expLevel: ControlLogLevelDebug,
		},
		"case insensitive": {
			yamlStr:  "dEbUg",
			expLevel: ControlLogLevelDebug,
		},
		"string unmarshal fails": {
			yamlErr: errors.New("test unmarshal yaml"),
			expErr:  errors.New("test unmarshal yaml"),
		},
		"string not a log level": {
			yamlStr: "garbage",
			expErr:  errors.New("not a valid log level"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			var level ControlLogLevel

			err := level.UnmarshalYAML(func(result interface{}) error {
				if strResult, ok := result.(*string); ok {
					*strResult = tc.yamlStr
				} else {
					t.Fatalf("%+v wasn't a string", result)
				}
				return tc.yamlErr
			})

			CmpErr(t, tc.expErr, err)
			AssertEqual(t, tc.expLevel, level, "")
		})
	}
}
