//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package engine

import (
	"regexp"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"
)

const (
	// NvmeMinBytesPerTarget is min NVMe pool allocation per target
	NvmeMinBytesPerTarget = 1 * humanize.GiByte
	// ScmMinBytesPerTarget is min SCM pool allocation per target
	ScmMinBytesPerTarget = 16 * humanize.MiByte
)

type (
	cmdLogger struct {
		logFn  func(string)
		prefix string
	}
)

func (cl *cmdLogger) Write(data []byte) (int, error) {
	if cl.logFn == nil {
		return 0, errors.New("no log function set in cmdLogger")
	}

	var msg string
	if cl.prefix != "" {
		msg = cl.prefix + " "
	}
	msg += string(data)
	cl.logFn(msg)
	return len(data), nil
}

const (
	logMasksStrMaxLen    = 1023
	logMasksStrAssignSep = ","
	logMasksStrAssignOp  = "="
)

var logMasksValidLevels = []string{
	"DEBUG", "DBUG", "INFO", "NOTE", "WARN", "ERROR", "ERR", "CRIT", "ALRT", "EMRG", "EMIT",
}

func isValidLevel(level string) bool {
	for _, l := range logMasksValidLevels {
		if strings.ToUpper(level) == l {
			return true
		}
	}

	return false
}

func errUnknownLogLevel(level string) error {
	return errors.Errorf("unknown log level %q want one of %v", level, logMasksValidLevels)
}

// ValidateLogMasks provides validation for log-masks string specifier.
//
// The input string should look like: PREFIX1=LEVEL1,PREFIX2=LEVEL2,...
// LEVELs are validated here whilst PREFIX (facility) is validated server side.
func ValidateLogMasks(masks string) error {
	if masks == "" {
		return nil
	}
	if len(masks) > logMasksStrMaxLen {
		return errors.Errorf("log masks string exceeds maximum length (%d>%d)",
			len(masks), logMasksStrMaxLen)
	}

	re := regexp.MustCompile(`^([a-zA-Z,=]+)$`)
	matches := re.FindStringSubmatch(masks)
	if matches == nil {
		return errors.Errorf("log masks has illegal characters: %q", masks)
	}

	for idx, tok := range strings.Split(masks, logMasksStrAssignSep) {
		if idx == 0 && !strings.Contains(tok, logMasksStrAssignOp) {
			if !isValidLevel(tok) {
				return errUnknownLogLevel(tok)
			}
			continue // first specifier can exclude facility "PREFIX="
		}

		facLevel := strings.Split(tok, logMasksStrAssignOp)
		if len(facLevel) != 2 {
			return errors.Errorf("illegal log mask assignment: want PREFIX=LEVEL got %q",
				tok)
		}
		if !isValidLevel(facLevel[1]) {
			return errUnknownLogLevel(facLevel[1])
		}
	}

	return nil
}
