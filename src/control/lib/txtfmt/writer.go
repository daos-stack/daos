//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package txtfmt

import (
	"fmt"
	"io"
)

// ErrWriter eliminates repetitive error handling by
// capturing an error and ignoring subsequent writes.
// The original error is available to be used by the caller.
//
// https://dave.cheney.net/2019/01/27/eliminate-error-handling-by-eliminating-errors
type ErrWriter struct {
	writer io.Writer
	Err    error
}

// NewErrWriter returns an initialized ErrWriter.
func NewErrWriter(w io.Writer) *ErrWriter {
	return &ErrWriter{writer: w}
}

func (w *ErrWriter) Write(data []byte) (int, error) {
	if w.Err != nil {
		return 0, w.Err
	}

	var n int
	n, w.Err = w.writer.Write(data)
	return n, w.Err
}

const (
	defaultPadCount = 2
)

type (
	// IndentWriter indents every line written to it.
	IndentWriter struct {
		writer    io.Writer
		padCount  uint
		inNewLine bool
	}

	// IndentWriterOption configures the IndentWriter.
	IndentWriterOption func(*IndentWriter)
)

// WithPadCount sets the indent width.
func WithPadCount(count uint) IndentWriterOption {
	return func(w *IndentWriter) {
		w.padCount = count
	}
}

// NewIndentWriter returns an initialized IndentWriter.
func NewIndentWriter(w io.Writer, opts ...IndentWriterOption) *IndentWriter {
	iw := &IndentWriter{
		writer:    w,
		padCount:  defaultPadCount,
		inNewLine: true,
	}

	for _, opt := range opts {
		opt(iw)
	}
	return iw
}

func (w *IndentWriter) Write(data []byte) (int, error) {
	if w.inNewLine {
		if len(data) > 0 && data[0] != '\n' {
			w.inNewLine = false
			padding := fmt.Sprintf("%*c", w.padCount, ' ')
			if _, err := w.writer.Write([]byte(padding)); err != nil {
				return 0, err
			}
		}
	}

	for i, c := range data {
		if c == '\n' {
			count, err := w.writer.Write(data[:i+1])
			if err != nil {
				return count, err
			}
			w.inNewLine = true
			wrote, err := w.Write(data[i+1:])
			return count + wrote, err
		}
	}

	return w.writer.Write(data)
}
