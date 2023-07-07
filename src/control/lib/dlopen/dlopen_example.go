//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build linux
// +build linux

package dlopen

// #include <string.h>
// #include <stdlib.h>
//
// int
// my_strlen(void *f, const char *s)
// {
//   size_t (*strlen)(const char *);
//
//   strlen = (size_t (*)(const char *))f;
//   return strlen(s);
// }
import "C"

import (
	"fmt"
	"unsafe"
)

func strlen(libs []string, s string) (int, error) {
	h, err := GetHandle(libs)
	if err != nil {
		return -1, fmt.Errorf(`couldn't get a handle to the library: %v`, err)
	}
	defer h.Close()

	f := "strlen"
	cs := C.CString(s)
	defer C.free(unsafe.Pointer(cs))

	strlen, err := h.GetSymbolPointer(f)
	if err != nil {
		return -1, fmt.Errorf(`couldn't get symbol %q: %v`, f, err)
	}

	len := C.my_strlen(strlen, cs)

	return int(len), nil
}
