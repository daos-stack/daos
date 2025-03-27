# Argobots

Argobots is a lightweight, low-level threading and tasking framework.

README.md should contain enough information to get you started with Argobots.

1. [Getting Started](#1-getting-started)
2. [Testing Argobots](#2-testing-argobots)
3. [Reporting Problems](#3-reporting-problems)
4. [Alternate Configure Options](#4-alternate-configure-options)
5. [Compiler Flags](#5-compiler-flags)
6. [Developer Builds](#6-developer-builds)

- More information about Argobots can be found at https://www.argobots.org
- Complete Argobots API can be found at https://www.argobots.org/doxygen/latest/

-------------------------------------------------------------------------------

## 1. Getting Started

The following instructions take you through a sequence of steps to get the
default configuration of Argobots up and running.  For compilation, Argobots
needs a C compiler (gcc is sufficient).

Also, you need to know what shell you are using since different shell has
different command syntax.  Command `echo $SHELL` prints out the current shell
used by your terminal program.

### (a) Preparation

(a.1) *If you downloaded a release tarball of Argobots*, unpack the tar file and
go to the top level directory:

```sh
    tar xzf argobots.tar.gz
    cd argobots

    ## If your `tar` doesn't accept the `z` option, use the following instead
    # gunzip argobots.tar.gz
    # tar xf argobots.tar
    # cd argobots
```

(a.2) *If you cloned Argobots from the GitHub page*, go to the top level
directory and create `configure`:

```sh
    cd argobots
    ./autogen.sh
```

### (b) Choose an installation directory

The installation directory should be non-existent or empty.
The following assumes `/home/USERNAME/argobots-install`

### (c) Configure Argobots specifying the installation directory

```sh
    ./configure --prefix=/home/USERNAME/argobots-install 2>&1 | tee c.txt

    ## If you are using csh or tcsh:
    # ./configure --prefix=/home/USERNAME/argobots-install |& tee c.txt
```

If a failure occurs, the configure command will display the error.  Most errors
are straight-forward to follow.

### (d) Build Argobots:

```sh
    make 2>&1 | tee m.txt

    ## If you are using csh or tcsh:
    # make |& tee m.txt
```

This step should succeed if there were no problems with the preceding step.
Check file `m.txt`.  If there were problems, do a `make clean` and then run
make again with `V=1`.

```sh
    make V=1 2>&1 | tee m.txt

    ## If you are using csh or tcsh:
    # make V=1 |& tee m.txt
```

If it does not work, go to step 3 below, for reporting the issue to the Argobots
developers and other users.

### (e) Install Argobots:

```sh
    make install 2>&1 | tee mi.txt

    ## If you are using csh or tcsh:
    # make install |& tee mi.txt
```

This step collects all required files in the `bin` subdirectory of the directory
specified by the prefix argument to configure.


## 2. Testing Argobots

To test Argobots, we package the Argobots test suite in the Argobots
distribution.  You can run the test suite in the `test` directory using:

```sh
    make check
```

The distribution also includes some Argobots examples.  You can run them in the
`examples` directory using:

```sh
    make check
```

If you run into any problems on running the test suite or examples, please
follow step 3 below for reporting them to the Argobots developers and other
users.


## 3. Reporting Problems

If you have problems with the installation or usage of Argobots, please follow
these steps:

(a) First visit the Frequently Asked Questions (FAQ) page at
https://github.com/pmodels/argobots/wiki/FAQ
to see if the problem you are facing has a simple solution.

(b) If you cannot find an answer on the FAQ page, look through previous email
threads on the discuss@argobots.org mailing list archive
(https://lists.argobots.org/mailman/listinfo/discuss).  It is likely someone
else had a similar problem, which has already been resolved before.

(c) If neither of the above steps work, please send an email to
discuss@argobots.org.  You need to subscribe to this list
(https://lists.argobots.org/mailman/listinfo/discuss) before sending an email.

Your email should contain the following files.  ONCE AGAIN, PLEASE COMPRESS
BEFORE SENDING, AS THE FILES CAN BE LARGE.  Note that, depending on which step
the build failed, some of the files might not exist.

```
    argobots/c.txt      (generated in step 1(c) above)
    argobots/m.txt      (generated in step 1(d) above)
    argobots/mi.txt     (generated in step 1(e) above)
    argobots/config.log (generated in step 1(c) above)
```

Finally, please include the actual error you are seeing when running the
application.  If possible, please try to reproduce the error with a smaller
application or benchmark and send that along in your bug report.

(d) If you have found a bug in Argobots, we request that you report it at our
github issues page (https://github.com/pmodels/argobots/issues).  Even if you
believe you have found a bug, we recommend you sending an email to
discuss@argobots.org first.


## 4. Alternate Configure Options

Argobots has a number of other features.  If you are exploring Argobots as part
of a development project, you might want to tweak the Argobots build with the
following configure options.  A complete list of configuration options can be
found using:

```sh
    ./configure --help
```


## 5. Compiler Flags

By default, Argobots automatically adds certain compiler optimizations to
CFLAGS.  The currently used optimization level is -O2.

This optimization level can be changed with the --enable-fast option passed to
configure.  For example, to build Argobots with -O3, one can simply do:

```sh
    ./configure --enable-fast=O3
```

For more details of `--enable-fast`, see the output of `./configure --help`.

For performance testing, we recommend the following flags:

```sh
    ./configure --enable-perf-opt --enable-affinity --disable-checks
```

For debugging, we recommend the following flags:

```sh
    ./configure --enable-fast=O0 --enable-debug=most
```


## 6. Developer Builds

For Argobots developers who want to directly work on the primary version control
system, there are a few additional steps involved (people using the release
tarballs do not have to follow these steps).  Details about these steps can be
found here: https://github.com/pmodels/argobots/wiki/Getting-and-Building
