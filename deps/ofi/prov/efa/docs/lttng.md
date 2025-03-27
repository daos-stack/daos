# LTTng Guide

This document serves a basic “getting started” guide for tracing Libfabric.
There’s way more to LTTng (in terms of both capability and complexity) than
what’s covered here. Please refer to the official [LTTng
Documentation](https://lttng.org/docs/v2.13/) and [man
pages](https://lttng.org/man/).

## Installation

The LTTng integration in the EFA provider source code requires LTTng-UST 2.13 or greater.

### Prerequisites

If these are missing, they should be fine to install via `yum` or other equivalent:

* [libuuid](https://sourceforge.net/projects/libuuid/)
* [popt](https://directory.fsf.org/wiki/Popt)
* [libxml2](http://www.xmlsoft.org/)
* [numactl](https://github.com/numactl/numactl) (Optional; configure LTTng-UST with `--disable-numa` otherwise)

For the purpose of this guide, we’ll install LTTng and any necessary local
dependencies to `~/.local`, but feel free to install wherever you like; just
ensure the prefix is in your `PATH`.

### Userspace RCU

```
git clone git://git.liburcu.org/userspace-rcu.git
cd userspace-rcu
./bootstrap
./configure --prefix=$HOME/.local
make -j install
ldconfig
```

### LTTng-UST

```
wget https://lttng.org/files/lttng-ust/lttng-ust-latest-2.13.tar.bz2
tar -xf lttng-ust-latest-2.13.tar.bz2
cd lttng-ust-2.13.*
./configure \
  --prefix=$HOME/.local \
  --disable-numa \
  PKG_CONFIG_PATH=$HOME/.local/lib/pkgconfig
make -j install
ldconfig
```

### LTTng-tools

```
wget https://lttng.org/files/lttng-tools/lttng-tools-latest-2.13.tar.bz2
tar -xf lttng-tools-latest-2.13.tar.bz2
cd lttng-tools-2.13.*
./configure \
  --prefix=$HOME/.local \
  PKG_CONFIG_PATH=$HOME/.local/lib/pkgconfig
make -j install
ldconfig
```

### Libfabric

Configure Libfabric as you normally would; just specify
`--with-lttng=$HOME/.local` in order for the `configure` script to find the
necessary LTTng libraries.

### Babeltrace2 (optional)

Babeltrace2 provides the simplest method for analyzing tracing data collected by
LTTng. Current installation instructions can also be found
[here](https://babeltrace.org/#bt2-get).

#### Prerequisites

* [GLib](https://developer.gnome.org/glib/) >= 2.28
* [elfutils](https://sourceware.org/elfutils/) >= 0.154

```
wget https://www.efficios.com/files/babeltrace/babeltrace2-2.0.3.tar.bz2
tar -xf babeltrace2-2.0.3.tar.bz2
cd babeltrace2-2.0.3
./configure
make -j install
```

## Collect Libfabric Tracing Data

### Start a [session daemon](https://lttng.org/docs/v2.13/#doc-lttng-sessiond)

```
lttng-sessiond --daemonize
```

See also: [`lttng-sessiond(8)`](https://lttng.org/man/8/lttng-sessiond/v2.13/)

### Create a [recording session](https://lttng.org/docs/v2.13/#doc-tracing-session)

```
lttng create my-libfabric-session
```

By default, LTTng will store tracing data in `$LTTNG_HOME/lttng-traces`
(`$LTTNG_HOME` defaults to `$HOME`). To specify an output directory, specify
`--output` when creating the session

```
lttng create my-libfabric-session --output=$HOME/efa-tracing-data
```

See also: [`lttng-create(1)`](https://lttng.org/man/1/lttng-create/v2.13)

### Enable tracepoints

To enable a specific tracepoint:

```
lttng enable-event --userspace efa:post_send
```

Or simply enable all pre-defined EFA tracepoints:

```
lttng enable-event --userspace efa:'*'
```

You can also list all available userspace tracepoints while Libfabric is running:

```
lttng list --userspace
```

If LTTng-UST was installed from source, `<installation prefix>/lib` will include
some [prebuilt helpers](https://lttng.org/docs/v2.13/#doc-prebuilt-ust-helpers)
for tracing common system functions, such as `malloc()` and `free()`.

See also:
[`lttng-enable-event(1)`](https://lttng.org/man/1/lttng-enable-event/v2.13),
[`lttng-list(1)`](https://lttng.org/man/1/lttng-list/v2.13/)

### Start recording

```
lttng start
```

In another terminal, run an application which calls Libfabric APIs. LTTng will
collect instrumentation data from any executed tracepoints.

See also: [`lttng-start(1)`](https://lttng.org/man/1/lttng-start/v2.13/)

### Stop/destroy session

```
lttng stop
```

```
lttng destroy
```

`destroy` implies `stop` as well. `destroy` will not delete tracing data; only
free up resources allocated to the LTTng tracing session.

See also: [`lttng-destroy(1)`](https://lttng.org/man/1/lttng-destroy/v2.13/),
[`lttng-stop(1)`](https://lttng.org/man/1/lttng-stop/v2.13)

## Tracing Data Analysis

There’s more than one way to [view and analyze recorded
events](https://lttng.org/docs/v2.13/#doc-viewing-and-analyzing-your-traces).
The simplest is to simply dump the events with Babeltrace2.

```
babeltrace2 ~/lttng-traces/my-libfabric-session*
```

```
babeltrace2 ~/lttng-traces/my-libfabric-session* | grep 'efa:'
```

See also: [`babeltrace2(1)`](https://babeltrace.org/docs/v2.0/man1/babeltrace2.1/)

## Custom Tracepoints

Refer also to LTTng’s documentation on [defining
tracepoints](https://lttng.org/docs/v2.13/#doc-defining-tracepoints).

### Tracepoint Definition (`efa_tp_def.h`)

```c
/* efa_tp_def.h */

LTTNG_UST_TRACEPOINT_EVENT(
    efa,
    foo,
    LTTNG_UST_TP_ARGS(
        int, bar
    ),
    LTTNG_UST_TP_FIELDS(
        lttng_ust_field_integer(int, bar, bar)
    )
)
```

This will create a tracepoint event named `efa:foo`

### Tracepoint Usage

In the source code:

```c
#include "efa_tp.h"

int baz(void)
{
    int bar;

    /* ... */

    efa_tracepoint(foo, bar);
}
```

To enable the event:

```
lttng enable-event --userspace efa:foo
```

### `printf()`-style tracing

LTTng also provides
[`lttng_ust_tracef()`](https://lttng.org/docs/v2.13/#doc-tracef) and
[`lttng_ust_tracelog()`](https://lttng.org/docs/v2.13/#doc-tracelog) APIs for
defining simple tracepoints without any of the tracepoint provider overhead. For
added convenience, there are `efa_tracef()` and `efa_tracelog()` macros in
`efa_tp.h`. These are intended for quick debugging and not recommended by LTTng
for permanent instrumentation.

```c
#include "efa_tp.h"

/* ... */

    efa_tracef("Hello from %s, line %d\n", __FILE__, __LINE__);
```

One tradeoff is that events traced from these APIs will be under the generic
provider:name `lttng_ust_tracef:event`

See also:
[`lttng_ust_tracef(3)`](https://lttng.org/man/3/lttng_ust_tracef/v2.13),
[`lttng_ust_tracelog(3)`](https://lttng.org/man/3/lttng_ust_tracelog/v2.13)

## See Also

* [`lttng(1)`](https://lttng.org/man/1/lttng/v2.13)
* [`lttng-ust(3)`](https://lttng.org/man/3/lttng-ust/v2.13/)
* [`lttng-concepts(7)`](https://lttng.org/man/7/lttng-concepts/v2.13)
