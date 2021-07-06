//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
package logging

import (
	"fmt"
	"log"
	"path"
)

func formatSource(file string, line, flags int) string {
	if file == "" || line == 0 {
		return ""
	}
	if flags&log.Lshortfile != 0 {
		file = path.Base(file)
	}
	return fmt.Sprintf("%s:%d", file, line)
}
