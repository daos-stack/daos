//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"os"
	"strings"

	"github.com/pkg/errors"
)

const (
	// UnsetLogMask defines an explicitly-unset log mask.
	UnsetLogMask = "UNSET"
	// DefaultDebugMask defines the basic debug mask.
	DefaultDebugMask = "DEBUG,MEM=ERR,OBJECT=ERR,PLACEMENT=ERR"
	// DefaultInfoMask defines the basic info mask.
	DefaultInfoMask = "INFO"
	// DefaultErrorMask defines the basic error mask.
	DefaultErrorMask = "ERROR"
)

// InitLogging initializes the DAOS logging system.
func InitLogging(masks ...string) (func(), error) {
	mask := strings.Join(masks, ",")
	if mask == "" {
		mask = DefaultInfoMask
	}
	if mask != UnsetLogMask {
		if err := SetLogMask(mask); err != nil {
			return func() {}, errors.Wrap(err, "failed to set DAOS logging mask")
		}
	}

	if rc := daos_debug_init(nil); rc != 0 {
		return func() {}, errors.Wrap(Status(rc), "daos_debug_init() failed")
	}

	return func() {
		daos_debug_fini()
	}, nil
}

// SetLogMask sets the DAOS logging mask.
func SetLogMask(mask string) error {
	return os.Setenv("D_LOG_MASK", mask)
}

// GetLogMask returns the DAOS logging mask, if set.
func GetLogMask() string {
	return os.Getenv("D_LOG_MASK")
}
