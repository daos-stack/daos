//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package txtfmt

import (
	"bytes"
	"fmt"
	"io"
	"text/tabwriter"
)

// TableRow is a map of string values to be printed, keyed by column title.
type TableRow map[string]string

// TableFormatter is a structure that formats string output for a table with
// labeled columns.
type TableFormatter struct {
	titles []string
	writer *tabwriter.Writer
	out    bytes.Buffer
}

// Init instantiates internal variables.
func (t *TableFormatter) Init() {
	t.InitWriter(&t.out)
}

// InitWriter optionally sets up the tabwriter to
// use the supplied io.Writer instead of the internal
// buffer.
func (t *TableFormatter) InitWriter(w io.Writer) {
	t.writer = tabwriter.NewWriter(w, 0, 0, 1, ' ', 0)
}

// SetColumnTitles sets the ordered column titles for the table.
func (t *TableFormatter) SetColumnTitles(c ...string) {
	if c == nil {
		t.titles = []string{}
		return
	}
	t.titles = c
}

// formatHeader formats a table header based on the column titles.
func (t *TableFormatter) formatHeader() {
	for _, title := range t.titles {
		fmt.Fprintf(t.writer, "%s\t", title)
	}
	fmt.Fprint(t.writer, "\n")
	for _, title := range t.titles {
		for i := 0; i < len(title); i++ {
			fmt.Fprint(t.writer, "-")
		}
		fmt.Fprint(t.writer, "\t")
	}
	fmt.Fprint(t.writer, "\n")
}

// Format generates an output string for the set of table rows provided. It
// includes a header with column titles, and fills only the requested columns
// in order.
func (t *TableFormatter) Format(table []TableRow) string {
	if len(t.titles) == 0 {
		return "" // nothing to format
	}

	t.formatHeader()

	for _, row := range table {
		for _, title := range t.titles {
			value, ok := row[title]
			if !ok {
				value = "None"
			}
			fmt.Fprintf(t.writer, "%s\t", value)
		}
		fmt.Fprint(t.writer, "\n")
	}

	t.writer.Flush()
	return t.out.String()
}

// NewTableFormatter creates and instantiates a new TableFormatter.
func NewTableFormatter(columnTitles ...string) *TableFormatter {
	f := &TableFormatter{}
	f.Init()
	f.SetColumnTitles(columnTitles...)
	return f
}
