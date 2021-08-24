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
	"syscall"

	"github.com/pkg/errors"
)

func dfsError(rc C.int) error {
	if rc == 0 {
		return nil
	}

	return syscall.Errno(rc)
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

	DfsPath   string `long:"dfs-path" short:"H" description:"DFS path relative to root of container, when using pool and container instead of --path and the UNS"`
	DfsPrefix string `long:"dfs-prefix" short:"I" description:"Optional prefix path to the root of the DFS container when using pool and container"`
}

func parseDFSParamsToAP(cmd *fsAttrCmd, ap *C.struct_cmd_args_s) error {
	if cmd.DfsPath != "" {
		if cmd.Path != "" {
			return errors.New("cannot use both --dfs-path and --path")
		}
		ap.dfs_path = C.CString(cmd.DfsPath)
	}
	if cmd.DfsPrefix != "" {
		if cmd.Path != "" {
			return errors.New("cannot use both --dfs-prefix and --path")
		}
		ap.dfs_prefix = C.CString(cmd.DfsPrefix)
	}
	return nil
}

func setupFSAttrCmd(cmd *fsAttrCmd, flags C.uint, op uint32) (*C.struct_cmd_args_s, *C.dfs_t, func(), error) {
	var err error
	cleanupFuncs := []func(){}
	runAll := func(funcs []func()) {
		for _, f := range funcs {
			f()
		}
	}
	defer func() {
		if err != nil {
			runAll(cleanupFuncs)
		}
	}()

	ap, deallocCmdArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return nil, nil, nil, err
	}
	cleanupFuncs = append(cleanupFuncs, deallocCmdArgs)

	if err := parseDFSParamsToAP(cmd, ap); err != nil {
		return nil, nil, nil, err
	}

	ap.fs_op = op

	cleanup, err := cmd.resolveAndConnect(flags, ap)
	if err != nil {
		return nil, nil, nil, err
	}
	cleanupFuncs = append(cleanupFuncs, cleanup)

	fsFlags := C.O_RDWR
	if flags == C.DAOS_COO_RO {
		fsFlags = C.O_RDONLY
	}

	dfs, unmount, err := mountDFS(ap, C.int(fsFlags))
	if err != nil {
		return nil, nil, nil, err
	}
	cleanupFuncs = append(cleanupFuncs, unmount)

	return ap, dfs, func() {
		runAll(cleanupFuncs)
	}, nil
}

type fsGetAttrCmd struct {
	fsAttrCmd
}

func (cmd *fsGetAttrCmd) Execute(_ []string) error {
	ap, dfs, cleanup, err := setupFSAttrCmd(&cmd.fsAttrCmd, C.DAOS_COO_RO, C.FS_GET_ATTR)
	if err != nil {
		return err
	}
	defer cleanup()

	obj, releaseObj, err := lookupDFSObj(ap.dfs_path, dfs, C.O_RDONLY)
	if err != nil {
		return err
	}
	defer releaseObj()

	objInfo, err := getDFSObjInfo(ap, dfs, obj)
	if err != nil {
		return err
	}

	if cmd.jsonOutputEnabled() {
		return cmd.outputJSON(objInfo, nil)
	}

	cmd.log.Infof("Object Class = %s\n", objInfo.ObjClass)
	cmd.log.Infof("Object Chunk Size = %d\n", objInfo.ChunkSize)

	return nil
}

type fsSetAttrCmd struct {
	fsAttrCmd
	ChunkSize   ChunkSizeFlag `long:"chunk-size" short:"z" description:"container chunk size"`
	ObjectClass ObjClassFlag  `long:"oclass" short:"o" description:"default object class"`
}

func (cmd *fsSetAttrCmd) Execute(_ []string) error {
	ap, dfs, cleanup, err := setupFSAttrCmd(&cmd.fsAttrCmd, C.DAOS_COO_RW, C.FS_SET_ATTR)
	if err != nil {
		return err
	}
	defer cleanup()

	obj, releaseObj, err := lookupDFSObj(ap.dfs_path, dfs, C.O_RDWR)
	if errors.Is(err, syscall.ENOENT) {
		if cmd.ChunkSize.Set {
			ap.chunk_size = cmd.ChunkSize.Size
		}
		if cmd.ObjectClass.Set {
			ap.oclass = cmd.ObjectClass.Class
		}
		_, releaseObj, err = createDFSObj(ap, dfs)
		if err != nil {
			return err
		}
		releaseObj()
		return nil
	} else if err != nil {
		return err
	}
	defer releaseObj()

	if cmd.ChunkSize.Set {
		if err := fsSetChunkSize(dfs, obj, uint64(cmd.ChunkSize.Size)); err != nil {
			return err
		}
	}

	if cmd.ObjectClass.Set {
		if err := fsSetObjClass(dfs, obj, uint16(cmd.ObjectClass.Class)); err != nil {
			return err
		}
	}

	return nil
}

type fsResetAttrCmd struct {
	fsAttrCmd
}

func (cmd *fsResetAttrCmd) Execute(_ []string) error {
	return fsResetAttr(&cmd.fsAttrCmd, C.FS_RESET_ATTR)
}

func fsResetAttr(cmd *fsAttrCmd, op uint32) error {
	ap, dfs, cleanup, err := setupFSAttrCmd(cmd, C.DAOS_COO_RW, op)
	if err != nil {
		return err
	}
	defer cleanup()

	obj, releaseObj, err := lookupDFSObj(ap.dfs_path, dfs, C.O_RDWR)
	if err != nil {
		return err
	}
	defer releaseObj()

	if op == C.FS_RESET_ATTR || op == C.FS_RESET_CHUNK_SIZE {
		if err := fsSetChunkSize(dfs, obj, 0); err != nil {
			return err
		}
	}

	if op == C.FS_RESET_ATTR || op == C.FS_RESET_OCLASS {
		if err := fsSetObjClass(dfs, obj, 0); err != nil {
			return err
		}
	}

	return nil
}

func fsSetChunkSize(dfs *C.dfs_t, obj *C.dfs_obj_t, size uint64) error {
	rc := C.dfs_obj_set_chunk_size(dfs, obj, 0, C.ulong(size))
	if rc != 0 {
		return errors.Wrap(dfsError(rc), "failed to set chunk size")
	}
	return nil
}

func fsSetObjClass(dfs *C.dfs_t, obj *C.dfs_obj_t, objClass uint16) error {
	rc := C.dfs_obj_set_oclass(dfs, obj, 0, C.ushort(objClass))
	if rc != 0 {
		return errors.Wrap(dfsError(rc), "failed to set object class")
	}
	return nil
}

type fsResetChunkSizeCmd struct {
	fsAttrCmd
}

func (cmd *fsResetChunkSizeCmd) Execute(_ []string) error {
	return fsResetAttr(&cmd.fsAttrCmd, C.FS_RESET_CHUNK_SIZE)
}

type fsResetOclassCmd struct {
	fsAttrCmd
}

func (cmd *fsResetOclassCmd) Execute(_ []string) error {
	return fsResetAttr(&cmd.fsAttrCmd, C.FS_RESET_OCLASS)
}
