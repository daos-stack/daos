//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

/*
#include "util.h"
*/
import "C"

import (
	"fmt"

	"github.com/pkg/errors"
)

func dfsError(rc C.int) error {
	if rc == 0 {
		return nil
	}

	strErr := C.strerror(rc)
	return errors.New(fmt.Sprintf("errno %d (%s)", rc, C.GoString(strErr)))
}

type fsCmd struct {
	Copy           fsCopyCmd           `command:"copy" description:"copy to and from a POSIX filesystem"`
	SetAttr        fsSetAttrCmd        `command:"set-attr" description:"set fs attributes"`
	GetAttr        fsGetAttrCmd        `command:"get-attr" description:"get fs attributes"`
	ResetAttr      fsResetAttrCmd      `command:"reset-attr" description:"reset fs attributes"`
	ResetChunkSize fsResetChunkSizeCmd `command:"reset-chunk-size" description:"reset fs chunk size"`
	ResetObjClass  fsResetOclassCmd    `command:"reset-oclass" description:"reset fs obj class"`
}

type fsCopyCmd struct {
	daosCmd

	Source   string `long:"src" short:"s" description:"copy source" required:"1"`
	Dest     string `long:"dst" short:"d" description:"copy destination" required:"1"`
	Preserve string `long:"preserve-props" short:"m" description:"preserve container properties, requires HDF5 library" required:"0"`
}

func (cmd *fsCopyCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	ap.src = C.CString(cmd.Source)
	defer freeString(ap.src)
	ap.dst = C.CString(cmd.Dest)
	defer freeString(ap.dst)
	if cmd.Preserve != "" {
		ap.preserve_props = C.CString(cmd.Preserve)
		defer freeString(ap.preserve_props)
	}

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

	DfsPath   string `long:"dfs-path" short:"H" description:"DFS path relative to root of container, when using pool and container instead of --path and the UNS"`
	DfsPrefix string `long:"dfs-prefix" short:"I" description:"Optional prefix path to the root of the DFS container when using pool and container"`
}

func setupFSAttrCmd(cmd *fsAttrCmd) (*C.struct_cmd_args_s, func(), error) {
	ap, deallocCmdArgs, err := allocCmdArgs(cmd.Logger)
	if err != nil {
		return nil, nil, err
	}

	if cmd.DfsPath != "" {
		if cmd.Path != "" {
			deallocCmdArgs()
			return nil, nil, errors.New("Cannot use both --dfs-path and --path")
		}
		ap.dfs_path = C.CString(cmd.DfsPath)
	}
	if cmd.DfsPrefix != "" {
		if cmd.Path != "" {
			deallocCmdArgs()
			return nil, nil, errors.New("Cannot use both --dfs-prefix and --path")
		}
		ap.dfs_prefix = C.CString(cmd.DfsPrefix)
	}

	return ap, deallocCmdArgs, nil
}

func fsOpString(op uint32) string {
	switch op {
	case C.FS_SET_ATTR:
		return "set-attr"
	case C.FS_RESET_ATTR:
		return "reset-attr"
	case C.FS_RESET_CHUNK_SIZE:
		return "reset-chunk-size"
	case C.FS_RESET_OCLASS:
		return "reset-oclass"
	case C.FS_GET_ATTR:
		return "get-attr"
	}
	return "unknown operation"
}

func fsModifyAttr(cmd *fsAttrCmd, op uint32, updateAP func(*C.struct_cmd_args_s)) error {
	ap, deallocCmdArgs, err := setupFSAttrCmd(cmd)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	flags := C.uint(C.DAOS_COO_RW)

	ap.fs_op = op
	if updateAP != nil {
		updateAP(ap)
	}

	cleanup, err := cmd.resolveAndConnect(flags, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	if err := dfsError(C.fs_dfs_hdlr(ap)); err != nil {
		return errors.Wrapf(err, "%s failed", fsOpString(op))
	}

	return nil
}

type fsSetAttrCmd struct {
	fsAttrCmd

	ChunkSize   ChunkSizeFlag `long:"chunk-size" short:"z" description:"container chunk size"`
	ObjectClass ObjClassFlag  `long:"oclass" short:"o" description:"default object class"`
}

func (cmd *fsSetAttrCmd) Execute(_ []string) error {
	return fsModifyAttr(&cmd.fsAttrCmd, C.FS_SET_ATTR, func(ap *C.struct_cmd_args_s) {
		if cmd.ObjectClass.Set {
			ap.oclass = cmd.ObjectClass.Class
		}
		if cmd.ChunkSize.Set {
			ap.chunk_size = cmd.ChunkSize.Size
		}
	})
}

type fsResetAttrCmd struct {
	fsAttrCmd
}

func (cmd *fsResetAttrCmd) Execute(_ []string) error {
	return fsModifyAttr(&cmd.fsAttrCmd, C.FS_RESET_ATTR, nil)
}

type fsResetChunkSizeCmd struct {
	fsAttrCmd
}

func (cmd *fsResetChunkSizeCmd) Execute(_ []string) error {
	return fsModifyAttr(&cmd.fsAttrCmd, C.FS_RESET_CHUNK_SIZE, nil)
}

type fsResetOclassCmd struct {
	fsAttrCmd
}

func (cmd *fsResetOclassCmd) Execute(_ []string) error {
	return fsModifyAttr(&cmd.fsAttrCmd, C.FS_RESET_OCLASS, nil)
}

type fsGetAttrCmd struct {
	fsAttrCmd
}

func (cmd *fsGetAttrCmd) Execute(_ []string) error {
	ap, deallocCmdArgs, err := setupFSAttrCmd(&cmd.fsAttrCmd)
	if err != nil {
		return err
	}
	defer deallocCmdArgs()

	ap.fs_op = C.FS_GET_ATTR
	flags := C.uint(C.DAOS_COO_RO)

	cleanup, err := cmd.resolveAndConnect(flags, ap)
	if err != nil {
		return err
	}
	defer cleanup()

	var attrs C.dfs_obj_info_t
	if err := dfsError(C.fs_dfs_get_attr_hdlr(ap, &attrs)); err != nil {
		return errors.Wrapf(err, "%s failed", fsOpString((ap.fs_op)))
	}

	var oclassName [16]C.char
	C.daos_oclass_id2name(attrs.doi_oclass_id, &oclassName[0])

	if cmd.jsonOutputEnabled() {
		jsonAttrs := &struct {
			ObjClass  string `json:"oclass"`
			ChunkSize uint64 `json:"chunk_size"`
		}{
			ObjClass:  C.GoString(&oclassName[0]),
			ChunkSize: uint64(attrs.doi_chunk_size),
		}
		return cmd.outputJSON(jsonAttrs, nil)
	}

	cmd.Infof("Object Class = %s", C.GoString(&oclassName[0]))
	cmd.Infof("Object Chunk Size = %d", attrs.doi_chunk_size)

	return nil
}
