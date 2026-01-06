//
// (C) Copyright 2022-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"github.com/desertbit/grumble"
)

func addAppCommands(app *grumble.App, ctx *DdbContext) {
	// Command: ls
	app.AddCommand(&grumble.Command{
		Name:      "ls",
		Aliases:   nil,
		Help:      "List containers, objects, dkeys, akeys, and values",
		LongHelp:  "",
		HelpGroup: "vos",
		Flags: func(f *grumble.Flags) {
			f.Bool("r", "recursive", false, "Recursively list the contents of the path")
			f.Bool("d", "details", false, "List more details of items in path")
		},
		Args: func(a *grumble.Args) {
			a.String("path", "Optional, list contents of the provided path", grumble.Default(""))
		},
		Run: func(c *grumble.Context) error {
			return ddbLs(ctx, c.Args.String("path"), c.Flags.Bool("recursive"), c.Flags.Bool("details"))
		},
		Completer: nil,
	})
	// Command: open
	app.AddCommand(&grumble.Command{
		Name:    "open",
		Aliases: nil,
		Help:    "Opens the vos file at <path>",
		LongHelp: `Opens the vos file at <path>. The '-w' option allows for modifying the vos file
with the rm, load, commit_ilog, etc commands. The path <path> should be an absolute path to the
pool shard. Part of the path is used to determine what the pool uuid is.`,
		HelpGroup: "vos",
		Flags: func(f *grumble.Flags) {
			f.Bool("w", "write_mode", false, "Open the vos file in write mode.")
		},
		Args: func(a *grumble.Args) {
			a.String("path", "Path to the vos file to open.")
		},
		Run: func(c *grumble.Context) error {
			return ddbOpen(ctx, c.Args.String("path"), c.Flags.Bool("write_mode"))
		},
		Completer: openCompleter,
	})
	// Command: version
	app.AddCommand(&grumble.Command{
		Name:      "version",
		Aliases:   nil,
		Help:      "Print ddb version",
		LongHelp:  "",
		HelpGroup: "",
		Run: func(c *grumble.Context) error {
			return ddbVersion(ctx)
		},
		Completer: nil,
	})
	// Command: close
	app.AddCommand(&grumble.Command{
		Name:      "close",
		Aliases:   nil,
		Help:      "Close the currently opened vos pool shard",
		LongHelp:  "",
		HelpGroup: "vos",
		Run: func(c *grumble.Context) error {
			return ddbClose(ctx)
		},
		Completer: nil,
	})
	// Command: superblock_dump
	app.AddCommand(&grumble.Command{
		Name:      "superblock_dump",
		Aliases:   nil,
		Help:      "Dump the pool superblock information",
		LongHelp:  "",
		HelpGroup: "vos",
		Run: func(c *grumble.Context) error {
			return ddbSuperblockDump(ctx)
		},
		Completer: nil,
	})
	// Command: value_dump
	app.AddCommand(&grumble.Command{
		Name:    "value_dump",
		Aliases: nil,
		Help:    "Dump a value",
		LongHelp: `Dump a value to the screen or file. The vos path should be a complete
path, including the akey and if the value is an array value it should include
the extent. If a path to a file was provided then the value will be written to
the file, else it will be printed to the screen.`,
		HelpGroup: "vos",
		Args: func(a *grumble.Args) {
			a.String("path", "VOS tree path to dump.")
			a.String("dst", "File path to dump the value to.", grumble.Default(""))
		},
		Run: func(c *grumble.Context) error {
			return ddbValueDump(ctx, c.Args.String("path"), c.Args.String("dst"))
		},
		Completer: nil,
	})
	// Command: rm
	app.AddCommand(&grumble.Command{
		Name:    "rm",
		Aliases: nil,
		Help:    "Remove a branch of the VOS tree.",
		LongHelp: `Remove a branch of the VOS tree. The branch can be anything from a container
and everything under it, to a single value.`,
		HelpGroup: "vos",
		Args: func(a *grumble.Args) {
			a.String("path", "VOS tree path to remove.")
		},
		Run: func(c *grumble.Context) error {
			return ddbRm(ctx, c.Args.String("path"))
		},
		Completer: nil,
	})
	// Command: value_load
	app.AddCommand(&grumble.Command{
		Name:    "value_load",
		Aliases: nil,
		Help:    "Load a value to a vos path. ",
		LongHelp: `Load a value to a vos path. This can be used to update
the value of an existing key, or create a new key. The <src> is a path to a
file on the file system. The <dst> is a vos tree path to a value where the
data will be loaded. If the <dst> path currently exists, then the destination
path must match the value type, meaning, if the value type is an array, then
the path must include the extent, otherwise, it must not.`,
		HelpGroup: "vos",
		Args: func(a *grumble.Args) {
			a.String("src", "Source file path.")
			a.String("dst", "Destination vos tree path to a value.")
		},
		Run: func(c *grumble.Context) error {
			return ddbValueLoad(ctx, c.Args.String("src"), c.Args.String("dst"))
		},
		Completer: nil,
	})
	// Command: ilog_dump
	app.AddCommand(&grumble.Command{
		Name:      "ilog_dump",
		Aliases:   nil,
		Help:      "Dump the ilog",
		LongHelp:  "",
		HelpGroup: "vos",
		Args: func(a *grumble.Args) {
			a.String("path", "VOS tree path to an object, dkey, or akey.")
		},
		Run: func(c *grumble.Context) error {
			return ddbIlogDump(ctx, c.Args.String("path"))
		},
		Completer: nil,
	})
	// Command: ilog_commit
	app.AddCommand(&grumble.Command{
		Name:      "ilog_commit",
		Aliases:   nil,
		Help:      "Process the ilog",
		LongHelp:  "",
		HelpGroup: "vos",
		Args: func(a *grumble.Args) {
			a.String("path", "VOS tree path to an object, dkey, or akey.")
		},
		Run: func(c *grumble.Context) error {
			return ddbIlogCommit(ctx, c.Args.String("path"))
		},
		Completer: nil,
	})
	// Command: ilog_clear
	app.AddCommand(&grumble.Command{
		Name:      "ilog_clear",
		Aliases:   nil,
		Help:      "Remove all the ilog entries",
		LongHelp:  "",
		HelpGroup: "vos",
		Args: func(a *grumble.Args) {
			a.String("path", "VOS tree path to an object, dkey, or akey.")
		},
		Run: func(c *grumble.Context) error {
			return ddbIlogClear(ctx, c.Args.String("path"))
		},
		Completer: nil,
	})
	// Command: dtx_dump
	app.AddCommand(&grumble.Command{
		Name:      "dtx_dump",
		Aliases:   nil,
		Help:      "Dump the dtx tables",
		LongHelp:  "",
		HelpGroup: "vos",
		Flags: func(f *grumble.Flags) {
			f.Bool("a", "active", false, "Only dump entries from the active table")
			f.Bool("c", "committed", false, "Only dump entries from the committed table")
		},
		Args: func(a *grumble.Args) {
			a.String("path", "VOS tree path to a container.")
		},
		Run: func(c *grumble.Context) error {
			return ddbDtxDump(ctx, c.Args.String("path"), c.Flags.Bool("active"), c.Flags.Bool("committed"))
		},
		Completer: nil,
	})
	// Command: dtx_cmt_clear
	app.AddCommand(&grumble.Command{
		Name:      "dtx_cmt_clear",
		Aliases:   nil,
		Help:      "Clear the dtx committed table",
		LongHelp:  "",
		HelpGroup: "vos",
		Args: func(a *grumble.Args) {
			a.String("path", "VOS tree path to a container.")
		},
		Run: func(c *grumble.Context) error {
			return ddbDtxCmtClear(ctx, c.Args.String("path"))
		},
		Completer: nil,
	})
	// Command: smd_sync
	app.AddCommand(&grumble.Command{
		Name:      "smd_sync",
		Aliases:   nil,
		Help:      "Restore the SMD file with backup from blob",
		LongHelp:  "",
		HelpGroup: "smd",
		Args: func(a *grumble.Args) {
			a.String("nvme_conf", "Path to the nvme conf file. (default /mnt/daos/daos_nvme.conf)", grumble.Default(""))
			a.String("db_path", "Path to the vos db. (default /mnt/daos)", grumble.Default(""))
		},
		Run: func(c *grumble.Context) error {
			return ddbSmdSync(ctx, c.Args.String("nvme_conf"), c.Args.String("db_path"))
		},
		Completer: nil,
	})
	// Command: vea_dump
	app.AddCommand(&grumble.Command{
		Name:      "vea_dump",
		Aliases:   nil,
		Help:      "Dump information from the vea about free regions",
		LongHelp:  "",
		HelpGroup: "vos",
		Run: func(c *grumble.Context) error {
			return ddbVeaDump(ctx)
		},
		Completer: nil,
	})
	// Command: vea_update
	app.AddCommand(&grumble.Command{
		Name:      "vea_update",
		Aliases:   nil,
		Help:      "Alter the VEA tree to mark a region as free.",
		LongHelp:  "",
		HelpGroup: "vos",
		Args: func(a *grumble.Args) {
			a.String("offset", "Block offset of the region to mark free.")
			a.String("blk_cnt", "Total blocks of the region to mark free.")
		},
		Run: func(c *grumble.Context) error {
			return ddbVeaUpdate(ctx, c.Args.String("offset"), c.Args.String("blk_cnt"))
		},
		Completer: nil,
	})
	// Command: dtx_act_commit
	app.AddCommand(&grumble.Command{
		Name:      "dtx_act_commit",
		Aliases:   nil,
		Help:      "Mark the active dtx entry as committed",
		LongHelp:  "",
		HelpGroup: "vos",
		Args: func(a *grumble.Args) {
			a.String("path", "VOS tree path to a container.")
			a.String("dtx_id", "DTX id of the entry to commit. ")
		},
		Run: func(c *grumble.Context) error {
			return ddbDtxActCommit(ctx, c.Args.String("path"), c.Args.String("dtx_id"))
		},
		Completer: nil,
	})
	// Command: dtx_act_abort
	app.AddCommand(&grumble.Command{
		Name:      "dtx_act_abort",
		Aliases:   nil,
		Help:      "Mark the active dtx entry as aborted",
		LongHelp:  "",
		HelpGroup: "vos",
		Args: func(a *grumble.Args) {
			a.String("path", "VOS tree path to a container.")
			a.String("dtx_id", "DTX id of the entry to abort. ")
		},
		Run: func(c *grumble.Context) error {
			return ddbDtxActAbort(ctx, c.Args.String("path"), c.Args.String("dtx_id"))
		},
		Completer: nil,
	})
	// Command: feature
	app.AddCommand(&grumble.Command{
		Name:      "feature",
		Aliases:   nil,
		Help:      "Manage vos pool features",
		LongHelp:  "",
		HelpGroup: "vos",
		Flags: func(f *grumble.Flags) {
			f.String("e", "enable", "", "Enable vos pool features")
			f.String("d", "disable", "", "Disable vos pool features")
			f.Bool("s", "show", false, "Show current features")
		},
		Args: func(a *grumble.Args) {
			a.String("path", "Optional, Path to the vos file", grumble.Default(""))
		},
		Run: func(c *grumble.Context) error {
			return ddbFeature(ctx, c.Args.String("path"), c.Flags.String("enable"), c.Flags.String("disable"), c.Flags.Bool("show"))
		},
		Completer: featureCompleter,
	})
	// Command: rm_pool
	app.AddCommand(&grumble.Command{
		Name:      "rm_pool",
		Aliases:   nil,
		Help:      "Remove a vos pool.",
		LongHelp:  "",
		HelpGroup: "vos",
		Args: func(a *grumble.Args) {
			a.String("path", "Optional, Path to the vos file", grumble.Default(""))
		},
		Run: func(c *grumble.Context) error {
			return ddbRmPool(ctx, c.Args.String("path"))
		},
		Completer: rmPoolCompleter,
	})
	// Command: dtx_act_discard_invalid
	app.AddCommand(&grumble.Command{
		Name:      "dtx_act_discard_invalid",
		Aliases:   nil,
		Help:      "Discard the active DTX entry's records if invalid.",
		LongHelp:  "",
		HelpGroup: "vos",
		Args: func(a *grumble.Args) {
			a.String("path", "VOS tree path to a container.")
			a.String("dtx_id", "DTX id of the entry to validate or 'all' to validate all active DTX entries.")
		},
		Run: func(c *grumble.Context) error {
			return ddbDtxActDiscardInvalid(ctx, c.Args.String("path"), c.Args.String("dtx_id"))
		},
		Completer: nil,
	})
	// Command: dev_list
	app.AddCommand(&grumble.Command{
		Name:      "dev_list",
		Aliases:   nil,
		Help:      "List all devices",
		LongHelp:  "",
		HelpGroup: "vos",
		Args: func(a *grumble.Args) {
			a.String("db_path", "Path to the vos db.")
		},
		Run: func(c *grumble.Context) error {
			return ddbDevList(ctx, c.Args.String("db_path"))
		},
		Completer: nil,
	})
	// Command dev_replace
	app.AddCommand(&grumble.Command{
		Name:      "dev_replace",
		Aliases:   nil,
		Help:      "Replace an old device with a new unused device",
		LongHelp:  "",
		HelpGroup: "vos",
		Args: func(a *grumble.Args) {
			a.String("db_path", "Path to the vos db.")
			a.String("old_dev", "Old device UUID.")
			a.String("new_dev", "New device UUID.")
		},
		Run: func(c *grumble.Context) error {
			return ddbDevReplace(ctx, c.Args.String("db_path"), c.Args.String("old_dev"), c.Args.String("new_dev"))
		},
		Completer: nil,
	})
}
