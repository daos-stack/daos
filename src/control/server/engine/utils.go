//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package engine

import (
	"regexp"
	"strings"
	"unicode/utf8"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
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

var validLogLevels = []string{
	"DEBUG", "DBUG", "INFO", "NOTE", "WARN", "ERROR", "ERR", "CRIT", "ALRT", "EMRG", "EMIT",
}

func isLogLevelValid(name string) bool {
	return common.Includes(validLogLevels, strings.ToUpper(name))
}

func errUnknownLogLevel(level string) error {
	return errors.Errorf("unknown log level %q want one of %v", level,
		validLogLevels)
}

func checkStrChars(in string) error {
	if !utf8.ValidString(in) {
		return errors.New("input is not valid UTF-8")
	}
	if len(in) > logMasksStrMaxLen {
		return errors.Errorf("string exceeds maximum length (%d>%d)",
			len(in), logMasksStrMaxLen)
	}

	re := regexp.MustCompile(`^([a-zA-Z,=]+)$`)
	matches := re.FindStringSubmatch(in)
	if matches == nil {
		return errors.Errorf("string has illegal characters: %q", in)
	}

	return nil
}

// ValidateLogMasks provides validation for log-masks string specifier.
//
// The input string should look like: PREFIX1=LEVEL1,PREFIX2=LEVEL2,...
// LEVELs are validated here whilst PREFIX (facility) is validated server side.
func ValidateLogMasks(masks string) error {
	if masks == "" {
		return nil
	}
	if err := checkStrChars(masks); err != nil {
		return errors.Wrap(err, "log masks")
	}

	for idx, tok := range strings.Split(masks, logMasksStrAssignSep) {
		if idx == 0 && !strings.Contains(tok, logMasksStrAssignOp) {
			if !isLogLevelValid(tok) {
				return errUnknownLogLevel(tok)
			}
			continue // first specifier can exclude facility "PREFIX="
		}

		facLevel := strings.Split(tok, logMasksStrAssignOp)
		if len(facLevel) != 2 {
			return errors.Errorf("illegal log mask assignment: want PREFIX=LEVEL got %q",
				tok)
		}
		if !isLogLevelValid(facLevel[1]) {
			return errUnknownLogLevel(facLevel[1])
		}
	}

	return nil
}

var (
	validLogStreams = []string{
		"ALL",                                                      // Select all debug streams
		"MD", "PL", "MGMT", "EPC", "DF", "REBUILD", "DAOS_DEFAULT", // DAOS debug streams
		"ANY", "TRACE", "MEM", "NET", "IO", // GURT debug streams
	}
	errAllWithOtherLogStream = errors.New("all stream identifier cannot be used with any other")
)

func isLogStreamValid(name string) bool {
	return common.Includes(validLogStreams, strings.ToUpper(name))
}

func errUnknownLogStream(stream string) error {
	return errors.Errorf("unknown log debug stream %q want one of %v", stream,
		validLogStreams)
}

// ValidateLogStreams provides validation for the stream names provided in the log-masks debug
// streams string input specifier.
//
// The input string should look like: STREAM1,STREAM2,...
func ValidateLogStreams(streams string) error {
	if streams == "" {
		return nil
	}
	if err := checkStrChars(streams); err != nil {
		return errors.Wrap(err, "debug streams")
	}

	tokens := strings.Split(streams, logMasksStrAssignSep)
	for _, tok := range tokens {
		if !isLogStreamValid(tok) {
			return errUnknownLogStream(tok)
		}
		if strings.ToUpper(tok) == "ALL" && len(tokens) != 1 {
			return errAllWithOtherLogStream
		}
	}

	return nil
}
