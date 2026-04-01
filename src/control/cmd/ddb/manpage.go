//
// Copyright 2025-2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"io"
	"os"

	"github.com/desertbit/grumble"
	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"
)

const manMacroSection = `." Miscellaneous Helper macros
.de Sp " Vertical space (when we can't use .PP)
.if t .sp .5v
.if n .sp
..
.de Vb " Begin verbatim text
.ft CW
.nf
.ne \$1
..
.de Ve " End verbatim text
.ft R
.fi
..
." ========================================================================
."`

const manArgsHeader = `.SH ARGUMENTS
.SS Application Arguments`

const manCmdsHeader = `.SH COMMANDS
.SS Available Commands`

const manPathSection = `.SH PATH
.SS VOS Tree Path
Many of the commands take a VOS tree path. The format for this path is [cont]/[obj]/[dkey]/[akey]/[extent].
.TP
.B cont
The full container uuid.
.TP
.B obj
The object id.
.TP
.B keys (akey, dkey)
There are multiple types of keys:
.RS
.IP "*" 4
.B string keys
are simply the string value. If the size of the key is greater than strlen(key), then
the size is included at the end of the string value. Example: 'akey{5}' is the key: akey with a null
terminator at the end.
.IP "*" 4
.B number keys
are formatted as '{[type]: NNN}' where type is 'uint8, uint16, uint32, or uint64'. NNN
can be a decimal or hex number. Example: '{uint32: 123456}'
.IP "*" 4
.B binary keys
are formatted as '{bin: 0xHHH}' where HHH is the hex representation of the binary key.
Example: '{bin: 0x1a2b}'
.RE
.TP
.B extent
For array values in the format {lo-hi}.
.SS Index Tree Path
.RE
To make it easier to navigate the tree, indexes can be used instead of the path part. The index is
in the format [i]. Indexes and actual path values can be used together.
.SS Path Examples
VOS tree path examples:
.sp
.EX
    /3550f5df-e6b1-4415-947e-82e15cf769af/939000573846355970.0.13.1/dkey/akey/[0\-1023]
.EE
.sp
Index tree path examples:
.sp
.EX
    [0]/[1]/[2]/[1]/[9]
.EE
.sp
Mixed tree path examples:
.sp
.EX
    /[0]/939000573846355970.0.13.1/[2]/akey{5}/[0\-1023]
.EE
.sp`

const manLoggingSection = `.SH LOGGING
The Go CLI and the C engine use separate logging systems with different log levels.
The \fI--debug=<log level>\fR option sets the log level for both systems to the closest matching
levels. The available log levels are: \fBTRACE\fR, \fBDEBUG\fR (or \fBDBUG\fR), \fBINFO\fR,
\fBNOTICE\fR (or \fBNOTE\fR), \fBWARN\fR, \fBERROR\fR (or \fBERR\fR), \fBCRIT\fR, \fBALRT\fR,
\fBFATAL\fR (or \fBEMRG\fR), and \fBEMIT\fR. The default log level is \fBERROR\fR.

Logs can be redirected to a file using the \fI--log_dir=<path>\fR option. Note that \fBERROR\fR
and above are always printed to the console, even when \fI--log_dir\fR is set.`

const manMdOnSsdSection = `.SH MD-ON-SSD MODE
.SS Overview
The MD-on-SSD workflow differs from PMEM mode. In PMEM mode, mount points are permanently
mounted and VOS files are created directly on PMEM devices. In MD-on-SSD mode, VOS files and
other DAOS metadata reside permanently on NVMe devices and must be recreated on a tmpfs mount
before ddb commands can operate on them.
.PP
The \fBprov_mem\fR command prepares the memory environment for ddb in MD-on-SSD mode. It
recreates the VOS files on the specified tmpfs mount so that ddb commands can be run against
them. Any modifications are automatically synced back to the NVMe devices; once finished, the
tmpfs mount can simply be unmounted to free memory.
.PP
.SS Synopsis
.Vb 1
\&    prov_mem [flags] db_path tmpfs_mount
.Ve
.SS Description
This command performs the following steps:
.IP "1." 4
Verifies the system is running in MD-on-SSD mode.
.IP "2." 4
Creates a tmpfs mount at the specified path (if not already mounted).
.IP "3." 4
Sets up the necessary directory structure.
.IP "4." 4
Recreates VOS pool target files on the tmpfs mount.
.SS Arguments
.TP
.B db_path
Path to the sys db.
.TP
.B tmpfs_mount
Path to the tmpfs mountpoint.
.SS Flags
.TP
.B \-s, \-\-tmpfs_size uint
Size of the tmpfs mount in GiB. Defaults to the total size of all VOS files.
.SS Examples
Prepare the memory environment with an auto-calculated tmpfs size:
.sp
.EX
    ddb prov_mem /path/to/sys/db /mnt/tmpfs
.EE
.sp
Prepare the memory environment with a specific tmpfs size of 16 GiB:
.sp
.EX
    ddb prov_mem -s 16 /path/to/sys/db /mnt/tmpfs
.EE
.sp
.SS Notes
.IP "*" 4
The \fBtmpfs_mount\fR path must not already be a mount point; otherwise the command will fail
with a "busy" error.
.IP "*" 4
If \fBtmpfs_size\fR is not specified, it is automatically calculated from the total size of
all VOS files.
.IP "*" 4
This command requires the system to be configured for MD-on-SSD mode.`

func fprintManPage(dest io.Writer, app *grumble.App, parser *flags.Parser) {
	fmt.Fprintln(dest, manMacroSection)

	parser.WriteManPage(dest)

	fmt.Fprintln(dest, manArgsHeader)
	for _, arg := range parser.Args() {
		fmt.Fprintf(dest, ".TP\n.B %s\n%s\n", arg.Name, arg.Description)
	}

	fmt.Fprintln(dest, manCmdsHeader)
	for _, cmd := range app.Commands().All() {
		if cmd.Name == "manpage" {
			continue
		}

		var cmdHelp string
		if cmd.LongHelp != "" {
			cmdHelp = cmd.LongHelp
		} else {
			cmdHelp = cmd.Help
		}
		fmt.Fprintf(dest, ".TP\n.B %s\n%s\n", cmd.Name, cmdHelp)
	}

	fmt.Fprintln(dest, manPathSection)

	fmt.Fprintln(dest, manMdOnSsdSection)

	fmt.Fprint(dest, manLoggingSection)
}

// addManPageCommand registers the 'manpage' grumble command with the app.
// When executed, it generates a man page in groff format, writing to stdout
// or to the file specified via the --output flag.
func addManPageCommand(app *grumble.App, p *flags.Parser) {
	app.AddCommand(&grumble.Command{
		Name:      "manpage",
		Help:      "Generate an application man page in groff format.",
		LongHelp:  "Generate an application man page in groff format. This command is used internally to generate the man page for the application and is not intended for general use.",
		HelpGroup: "",
		Flags: func(a *grumble.Flags) {
			a.String("o", "output", "", "Output file for the man page. If not provided, the man page will be printed to stdout.")
		},
		Run: func(c *grumble.Context) error {
			dest := os.Stdout
			if c.Flags.String("output") != "" {
				fd, err := os.Create(c.Flags.String("output"))
				if err != nil {
					return errors.Wrapf(err, "Error creating file %q", c.Flags.String("output"))
				}
				defer func() {
					err = fd.Close()
					if err != nil {
						fmt.Fprintf(os.Stderr, "Error closing file %q: %s\n", c.Flags.String("output"), err)
					}
				}()
				dest = fd
			}

			fprintManPage(dest, app, p)
			return nil
		},
		Completer: nil,
	})
}
