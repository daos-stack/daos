//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

/*
#include <daos.h>

#include "daos_hdlr.h"
*/
import "C"

import (
	"github.com/pkg/errors"
)

type fsCmd struct {
	Copy fsCopyCmd `command:"copy" description:"copy to and from a POSIX filesystem"`
}

type fsCopyCmd struct {
	daosCmd

	Source string `long:"src" short:"s" description:"copy source" required:"1"`
	Dest   string `long:"dst" short:"d" description:"copy destination" required:"1"`
}

func (cmd *fsCopyCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	ap.src = C.CString(cmd.Source)
	defer freeString(ap.src)
	ap.dst = C.CString(cmd.Dest)
	defer freeString(ap.dst)

	ap.fs_op = C.FS_COPY
	rc := C.fs_copy_hdlr(ap)
	if err := daosError(rc); err != nil {
		return errors.Wrapf(err,
			"failed to copy %s -> %s",
			cmd.Source, cmd.Dest)
	}

	return nil
}
