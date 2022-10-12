//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"github.com/desertbit/grumble"
)

/*
 #include <ddb.h>
 */
import "C"

func buildApp(ctx *C.struct_ddb_ctx) *grumble.App {
	description := `The DAOS Debug Tool (ddb) allows a user to navigate through and modify
a file in the VOS format. In order to modify the file, the '-w' option must
be included when opening the vos file.

Many of the commands take a vos tree path. The format for this path
is 'cont_uuid/obj_id/dkey/akey/recx'. The keys currently only support string
keys. The recx for array values is the format {lo-hi}. To make it easier to
navigate the tree, indexes can be used instead of the path part. The index
is in the format '[i]', for example '[0]/[0]/[0]'`

	var app = grumble.New(&grumble.Config{
		Name:        "ddb",
		Description:           description,
		Flags:                 nil,
		HistoryFile:           "/tmp/.ddb.hist",
		NoColor:               true,
		Prompt:                "$ ",
	})

	app.AddCommand(&grumble.Command {
		Name:      "ls",
		Aliases:   nil,
		Help:      "List containers, objects, dkeys, akeys, and values",
		LongHelp:  "",
		HelpGroup: "",
		Flags: func(f *grumble.Flags) {
			f.Bool("r", "recursive", false, "Recursively list the contents of the path")

		},
		Args: func(a *grumble.Args) {
			a.String("path", "Optional, list contents of the provided path", grumble.Default(""))
		},
		Run: func(c *grumble.Context) error {
			return ls_wrapper(ctx, c.Args.String("path"), c.Flags.Bool("recursive"))
		},
		Completer: nil,

	})
	app.AddCommand(&grumble.Command {
		Name:      "open",
		Aliases:   nil,
		Help:      "Opens the vos file at <path>",
		LongHelp:  `Opens the vos file at <path>. The '-w' option allows for modifying the vos file
with the rm, load, commit_ilog, etc commands. The path <path> should be an absolute path to the
pool shard. Part of the path is used to determine what the pool uuid is. `,
		HelpGroup: "",
		Flags: func(f *grumble.Flags) {
			f.Bool("w", "write_mode", false,
				"Open the vos file in write mode.")

		},
		Args: func(a *grumble.Args) {
			a.String("path", "Path to the vos file to open. ")
		},
		Run: func(c *grumble.Context) error {
			return open_wrapper(ctx, c.Args.String("path"), c.Flags.Bool("write_mode"))
		},
		Completer: openCompleter,
	})
	app.AddCommand(&grumble.Command {
		Name:      "close",
		Aliases:   nil,
		Help:      "Close the currently opened vos pool shard",
		LongHelp:  "",
		HelpGroup: "",
		Run: func(c *grumble.Context) error {
			return close_wrapper(ctx)
		},
		Completer: nil,
	})
	app.AddCommand(&grumble.Command {
		Name:      "dump_superblock",
		Aliases:   nil,
		Help:      "Dump the pool superblock information",
		LongHelp:  "",
		HelpGroup: "",
		Run: func(c *grumble.Context) error {
			return dump_superblock_wrapper(ctx)
		},
		Completer: nil,
	})
	app.AddCommand(&grumble.Command {
		Name:      "dump_value",
		Aliases:   nil,
		Help:      "Dump a value to a file",
		LongHelp:  `Dump a value to a file. The vos path should be a complete
path, including the akey and if the value is an array value it should include
the extent.`,
		HelpGroup: "",
		Args: func(a *grumble.Args) {
			a.String("path", "VOS tree path to dump.")
			a.String("dst", "File path to dump the value to.")
		},
		Run: func(c *grumble.Context) error {
			return dump_value_wrapper(ctx, c.Args.String("path"), c.Args.String("dst"))
		},
		Completer: nil,
	})
	app.AddCommand(&grumble.Command {
		Name:      "rm",
		Aliases:   nil,
		Help:      "Remove a branch of the VOS tree.",
		LongHelp:  `Remove a branch of the VOS tree. The branch can be anything from a container
and everything under it, to a single value.`,
		HelpGroup: "",
		Args: func(a *grumble.Args) {
			a.String("path", "VOS tree path to remove.")
		},
		Run: func(c *grumble.Context) error {
			return rm_wrapper(ctx, c.Args.String("path"))
		},
		Completer: nil,
	})
	app.AddCommand(&grumble.Command {
		Name:      "load",
		Aliases:   nil,
		Help:      "Load a value to a vos path.",
		LongHelp:  `Load a value to a vos path. This can be used to update
the value of an existing key, or create a new key. The <src> is a path to a
file on the file system. The <dst> is a vos tree path to a value where the
data will be loaded. If the <dst> path currently exists, then the destination
path must match the value type, meaning, if the value type is an array, then
the path must include the extent, otherwise, it must not. `,
		HelpGroup: "",
		Args: func(a *grumble.Args) {
			a.String("src", "Source file path that contains.")
			a.String("dst", "Destination vos tree path to a value.")
		},
		Run: func(c *grumble.Context) error {
			return load_wrapper(ctx, c.Args.String("src"), c.Args.String("dst"))
		},
		Completer: nil,
	})
	app.AddCommand(&grumble.Command {
		Name:      "dump_ilog",
		Aliases:   nil,
		Help:      "Dump the ilog",
		LongHelp:  "",
		HelpGroup: "",
		Args: func(a *grumble.Args) {
			a.String("path", "VOS tree path to an object, dkey, or akey.")
		},
		Run: func(c *grumble.Context) error {
			return dump_ilog_wrapper(ctx, c.Args.String("path"))
		},
		Completer: nil,
	})
	app.AddCommand(&grumble.Command {
		Name:      "commit_ilog",
		Aliases:   nil,
		Help:      "Process the ilog",
		LongHelp:  "",
		HelpGroup: "",
		Args: func(a *grumble.Args) {
			a.String("path", "VOS tree path to an object, dkey, or akey.")
		},
		Run: func(c *grumble.Context) error {
			return commit_ilog_wrapper(ctx, c.Args.String("path"))
		},
		Completer: nil,
	})
	app.AddCommand(&grumble.Command {
		Name:      "rm_ilog",
		Aliases:   nil,
		Help:      "Remove all the ilog entries",
		LongHelp:  "",
		HelpGroup: "",
		Args: func(a *grumble.Args) {
			a.String("path", "VOS tree path to an object, dkey, or akey.")
		},
		Run: func(c *grumble.Context) error {
			return rm_ilog_wrapper(ctx, c.Args.String("path"))
		},
		Completer: nil,
	})
	app.AddCommand(&grumble.Command {
		Name:      "dump_dtx",
		Aliases:   nil,
		Help:      "Dump the dtx tables",
		LongHelp:  "",
		HelpGroup: "",
		Flags: func(f *grumble.Flags) {
			f.Bool("a", "active", false, "Only dump entries from the active table")
			f.Bool("c", "committed", false, "Only dump entries from the committed table")
		},
		Args: func(a *grumble.Args) {
			a.String("path", "VOS tree path to a container.")
		},
		Run: func(c *grumble.Context) error {
			return dump_dtx_wrapper(ctx, c.Args.String("path"), c.Flags.Bool("active"),
				c.Flags.Bool("committed"))
		},
		Completer: nil,
	})
	app.AddCommand(&grumble.Command {
		Name:      "clear_cmt_dtx",
		Aliases:   nil,
		Help:      "Clear the dtx committed table",
		LongHelp:  "",
		HelpGroup: "",
		Args: func(a *grumble.Args) {
			a.String("path", "VOS tree path to a container.")
		},
		Run: func(c *grumble.Context) error {
			return clear_cmt_dtx_wrapper(ctx, c.Args.String("path"))
		},
		Completer: nil,
	})
	app.AddCommand(&grumble.Command {
		Name:      "smd_sync",
		Aliases:   nil,
		Help:      "Restore the SMD file with backup from blob",
		LongHelp:  "",
		HelpGroup: "",
		Args: func(a *grumble.Args) {
			a.String("nvme_conf", "Path to the nvme conf file. (/mnt/daos/daos_nvme.conf)",
				grumble.Default(""))
			a.String("db_path", "Path to the vos db. (/mnt/daos)", grumble.Default(""))
		},
		Run: func(c *grumble.Context) error {
			return smd_sync_wrapper(ctx, c.Args.String("nvme_conf"), c.Args.String("db_path"))
		},
		Completer: nil,
	})
	app.AddCommand(&grumble.Command {
		Name:      "dump_vea",
		Aliases:   nil,
		Help:      "Dump information from the vea about free regions",
		LongHelp:  "",
		HelpGroup: "",
		Run: func(c *grumble.Context) error {
			return dump_vea_wrapper(ctx)
		},
		Completer: nil,
	})
	app.AddCommand(&grumble.Command {
		Name:      "update_vea",
		Aliases:   nil,
		Help:      "Alter the VEA tree to mark a region as free.",
		LongHelp:  "",
		HelpGroup: "",
		Args: func(a *grumble.Args) {
			a.String("offset", "Block offset of the region to mark free.")
			a.String("blk_cnt", "Total blocks of the region to mark free.")
		},
		Run: func(c *grumble.Context) error {
			return update_vea_wrapper(ctx, c.Args.String("offset"), c.Args.String("blk_cnt"))
		},
		Completer: nil,
	})
	app.AddCommand(&grumble.Command {
		Name:      "dtx_commit",
		Aliases:   nil,
		Help:      "Mark the active dtx entry as committed",
		LongHelp:  "",
		HelpGroup: "",
		Args: func(a *grumble.Args) {
			a.String("path", "VOS tree path to a container.")
			a.String("dtx_id", "The dtx id of the entry to commit. ")
		},
		Run: func(c *grumble.Context) error {
			return dtx_commit_wrapper(ctx, c.Args.String("path"), c.Args.String("dtx_id"))
		},
		Completer: nil,
	})
	app.AddCommand(&grumble.Command {
		Name:      "dtx_abort",
		Aliases:   nil,
		Help:      "Mark the active dtx entry as aborted",
		LongHelp:  "",
		HelpGroup: "",
		Args: func(a *grumble.Args) {
			a.String("path", "VOS tree path to a container.")
			a.String("dtx_id", "The dtx id of the entry to abort. ")
		},
		Run: func(c *grumble.Context) error {
			return dtx_abort_wrapper(ctx, c.Args.String("path"), c.Args.String("dtx_id"))
		},
		Completer: nil,
	})

	return app
}
