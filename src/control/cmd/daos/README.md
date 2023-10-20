# DAOS CLI

The source files in this directory are used to build the command-line interface for DAOS ($PREFIX/bin/daos). This utility is intended to be used by both admin and unprivileged users. It leverages the <a href="../../../client/api/README.md">libdaos client library</a> in order to communicate with DAOS Data Plane servers via the storage fabric.

## Implementation Details

This version of the daos utility is implemented using a hybrid approach where the user interface is written in Go and uses the <a href="https://github.com/jessevdk/go-flags">go-flags</a> argument-parsing library to handle processing of command-line arguments and other UI-related tasks. The frontend code then uses <a href="https://golang.org/cmd/cgo/">cgo</a> to interface with the C-based backend handlers.

The original version of this utility was implemented entirely in C. There were advantages to this approach, particularly that the language is familiar to developers who primarily work on the DAOS client and server code. The pure-C implementation does suffer from some problems, however. Notably, the UI is very different from other user-facing utilities (dmg, etc.), and adding usability-improving features (tab completion, etc.) is much more difficult in C than it is in Go.

## Layout/Organization

The frontend code under src/control/cmd/daos is organized along the following lines:
  * main.go: The entry point to the program. Handles parsing of arguments and delegating to subcommand handlers. Most feature development efforts should not need to make any modifications to this file.
  * container.go: This is where most of the container subcommand (`daos cont ...`) handlers are. The top-level `containerCmd` struct defines handlers for each of the subcommands.
  * filesystem.go: This file contains the handlers for the `daos fs ...` family of commands. The top-level `fsCmd` struct defines handlers for each of the subcommands.
  * pool.go: This file contains the handlers for the `daos pool` family of commands. The top-level `poolCmd` struct defines handlers for each of the subcommands.

The remainder of the files contain shared utility code or have broken out some of the container subcommand handling in order to reduce the size of container.go.

The backend code is entirely written in C and for the moment is organized in the src/utils directory. The following files are linked into the `daos_cmd_hdlrs` library so that they can be called from the Go frontend:
  * daos_autotest.c
  * daos_dfs_hdlr.c
  * daos_hdlr.c

Longer-term, it would probably make sense to move the source files for `daos_cmd_hdlrs` under src/control/cmd/daos so that they live alongside the frontend code and can be compiled directly into the new daos binary in order to remove the need for the shared library. We could also consider implementing more of the backend logic in Go, but this may not necessarily make sense in many cases (e.g. data mover).

## Adding New Features

Adding new features should hopefully be fairly straightforward. The new frontend was designed to reduce a lot of the boilerplate required for pool/container connection and error checking, in addition to simplifying argument parsing and unifying the interfaces.

As an example, we can look at adding a new container subcommand such that running `daos cont scrub` will invoke a libdaos API for scrubbing a container, whatever that means.

The first question to answer is whether or not we need a C-based handler to wrap the API call, or if we can call the API directly from Go. This is somewhat of a philosophical question and it really depends on how comfortable the implementer is in working with Go. For the purposes of this example, let's assume that the implementer is going to add a new handler named `cont_scrub_hdlr()` to daos_hdlr.c:

In src/utils/daos_hdlr.h:
```C
struct cmd_args_s {
	/* existing fields */
	char	*scrub_level; /* how much scrubbing to do (string name) */
}

...

int cont_scrub_hdlr(struct cmd_args_s *ap);
```

In src/utils/daos_hdlr.c:
```C
int
cont_scrub_hdlr(struct cmd_args_s *ap)
{
	int			rc;
	daos_cont_scrub_lvl	level;

	assert(ap != NULL);
	assert(ap->c_op == CONT_SCRUB);
	assert(ap->scrub_level != NULL);

	level = daos_cont_scrub_lvl_name2val(ap->scrub_level);
	if (level == NULL) {
		fprintf(ap->errstream,
			"unable to resolve %s to scrub level", ap->scrub_level);
		D_GOTO(out, rc = -DER_INVAL);
	}

	/* do some other stuff to prep/check */

	rc = daos_cont_scrub(ap->cont, level);

	/* do some error handling */

out:
	return rc;
}
```

In src/control/cmd/daos/container.go:
```Go
type containerCmd struct {
	// ... existing container subscommands

	Scrub containerScrubCmd `command:"scrub" description:"scrub-a-dub"`
}

...
// Each subcommand is defined in its own struct which contains the details
// of which arguments are relevant to that subcommand.
type containerScrubCmd struct {
	// Embedding existingContainerCmd gives you access to all of the
	// methods/fields of that struct here in this struct. This way we
	// can reduce or eliminate a lot of boilerplate. If you look at the
	// definition of existingContinerCmd you will see that it takes
	// care of parsing --pool/--cont arguments, for example.
	existingContainerCmd

	// We only need to define arguments specific to this subcommand.
	// Note that we can define long/short arguments, and optionally
	// define a default that will show in the help output.
	Level string `long:"level" short:"l" description:"how clean it should be" default:"reasonably"`
}

// Every command implements the go-flags.Commander interface, which is
// to say that they must define a method with the following signature:
func (cmd *containerScrubCmd) Execute(_ []string) error {
	// Here is where we can do some input validation before
	// attempting to connect to the pool/container and invoking
	// the scrub handler.

	// Next, allocate/init a cmd_args_s struct for use with
	// cont_scrub_hdlr():
	ap, freeArgs, err := allocCmdArgs(cmd.log)
	if err != nil {
		return err
	}
	// This will automatically free most C-based memory allocated
	// along the way, and is always invoked regardless of error
	// status.
	defer freeArgs()

	// Note that in Go, accessing a field is always done via
	// "." regardless of whether or not the receiver is a pointer.
	//
	// Also note that Go strings are a distinct type and are not
	// NUL-terminated, so we need to create a C-style string for
	// cmd_args_s.
	ap.scrub_level = C.CString(cmd.Level)
	// The freeArgs() closure only frees fields used with all
	// handlers. It's up to the handler implementer to free
	// C memory allocated in the handler.
	defer freeString(ap.scrub_level)

	// Now attempt to connect to the pool/container, populating
	// the handle/uuid fields in ap:
	disconnect, err := cmd.resolveAndConnect(ap)
	if err != nil {
		return err
	}
	defer disconnect() // Always clean up on the way out.

	// We call into C from Go using the `C.` prefix. The
	// return value is an int as returned by the function.
	rc := C.cont_scrub_hdlr(ap)
	if err := daosError(rc); err != nil {
		return errors.Errorf("failed to scrub container %s",
			cmd.Container())
	}

	return nil
}
```

There are more advanced possibilities here using the go-flags package. For example, we could change the Level argument from a simple string into a custom type that implements the `flags.Completer` interface in order to allow for tab-completion of valid scrub level names.
