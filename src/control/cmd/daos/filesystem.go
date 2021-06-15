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
	"os"

	"github.com/pkg/errors"
)

type fsCmd struct {
	Copy           fsCopyCmd `command:"copy" description:"copy to and from a POSIX filesystem"`
	SetAttr        fsAttrCmd `command:"set-attr" description:"set fs attributes"`
	GetAttr        fsAttrCmd `command:"get-attr" description:"get fs attributes"`
	ResetAttr      fsAttrCmd `command:"reset-attr" description:"reset fs attributes"`
	ResetChunkSize fsAttrCmd `command:"reset-chunk-size" description:"reset fs chunk size"`
	ResetObjClass  fsAttrCmd `command:"reset-oclass" description:"reset fs obj class"`
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

type fsAttrCmd struct {
	existingContainerCmd

	ChunkSize   chunkSizeFlag `long:"chunk-size" short:"z" description:"container chunk size"`
	ObjectClass objClassFlag  `long:"oclass" short:"o" description:"default object class"`
}

func (cmd *fsAttrCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	op := os.Args[2]
	switch op {
	case "set-attr":
		ap.fs_op = C.FS_SET_ATTR
		if cmd.ObjectClass.set {
			ap.oclass = cmd.ObjectClass.class
		}
		if cmd.ChunkSize.set {
			ap.chunk_size = cmd.ChunkSize.size
		}
	case "get-attr":
		ap.fs_op = C.FS_GET_ATTR
	case "reset-attr":
		ap.fs_op = C.FS_RESET_ATTR
	case "reset-chunk-size":
		ap.fs_op = C.FS_RESET_CHUNK_SIZE
		if !cmd.ChunkSize.set {
			return errors.New("--chunk-size not set")
		}
		ap.chunk_size = cmd.ChunkSize.size
	case "reset-oclass":
		ap.fs_op = C.FS_RESET_OCLASS
		if !cmd.ObjectClass.set {
			return errors.New("--oclass not set")
		}
		ap.oclass = cmd.ObjectClass.class
	default:
		return errors.Errorf("unknown fs op %q", op)
	}

	cleanup, err := cmd.resolveAndConnect(ap)
	if err != nil {
		return err
	}
	defer cleanup()

	rc := C.fs_dfs_hdlr(ap)
	if err := daosError(rc); err != nil {
		return err
	}

	return nil
}
