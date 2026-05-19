# DAOS Documentation

Documentation on DAOS is available in the following places:

* The [https://daos.io/](https://daos.io/) website contains general
  information about the DAOS Foundation, a subproject of the
  Linux Foundation. This website is maintained by the
  DAOS Foundation and uses Wordpress for content management.

* The DAOS Community Wiki [https://wiki.daos.io/](https://wiki.daos.io/)
  is a redirection to
  [https://daosio.atlassian.net/wiki/spaces/DC](
   https://daosio.atlassian.net/wiki/spaces/DC),
  an Atlassian Wiki space.

* Documentation for end users and administrators of DAOS is
  available at [https://docs.daos.io/](https://docs.daos.io/).
  This website provides versioned documentation for each DAOS release.
  Its web pages are created from the contents in the
  [daos-stack/daos/docs/](https://github.com/daos-stack/daos/tree/master/docs)
  trees of the DAOS release branches in Github.
  This website also cross-reference the "DAOS Internals" documentation
  (in the "Developer Zone" section).

* The `dmg` and `daos` commands have man-pages that are created at
  DAOS build time through hidden `manpage` options of these commands.
  The contents of these man-pages is embedded in the Go code for
  the commands. The man-pages currently only output concatenated
  lists of all subcommands and their options.
  See
  [src/control/cmd/dmg/README.md](https://github.com/daos-stack/daos/blob/master/src/control/cmd/dmg/README.md)
  and
  [src/control/cmd/daos/README.md](https://github.com/daos-stack/daos/blob/master/src/control/cmd/daos/README.md).

* Most DAOS commands and subcommands have a `--help` option which
  prints one-line descriptions of the (sub)command and its options.
  The contents of these help texts is embedded in the Go code
  (or C code) for the commands.

* Developer-facing "DAOS Internals" documentation of the
  DAOS software is maintained as Markdown files within the
  [daos-stack/daos/src/](https://github.com/daos-stack/daos/tree/master/src)
  source tree of the DAOS Github repository.
  Make sure to access the release branch (`master`, `release/2.8`, etc.)
  for the version of DAOS you are working with.

* DAOS API documentation is generated from the DAOS source files using
  `doxygen`. Running `doxygen` during the website deployment process for
  [https://docs.daos.io/](https://docs.daos.io/)
  creates HTML pages in `docs/doxygen/html/`,
  which are then made available in the "Developer Zone" section of
  [https://docs.daos.io/](https://docs.daos.io/).

The following sections describe how the multi-versioned documentation
on [https://docs.daos.io/](https://docs.daos.io/)
is generated from the documentation Markdown files in the Github
`daos-stack/daos/doc/` tree (using the `mkdocs` and `mike` tools,
and the `doxygen` tool for the API documentation).

## Configuration files in Github

### Mkdocs configuration

The [MkDocs](https://www.mkdocs.org/) tool is a static site generator
that creates web pages from Markdown files.
The `mkdocs` command is driven by a single configuration file,
[mkdocs.yml](https://github.com/daos-stack/daos/tree/master/mkdocs.yml)
in the toplevel directory of the DAOS project on github.
Among other project information, this file defines the
menu structure for the navigation bar of the website that is being created.
Each DAOS release branch uses its own version of `mkdocs.yml`,
as some information in that file is version specific or uses URLs
that are pointing to that specific DAOS version.

!!! note
Absolute paths or hardcoded version numbers in the `mkdocs.yml` file
should **not** be changed to relative URLs/paths,
as that will break some of the processing by downstream tools.
These specifics need to be updated once when a new release branch is
created, and again when an existing release branch that was under
development reaches General Availability (GA).
Examples with detailed steps can be found below.

The `mkdocs` tool creates a `sites` subdirectory in the toplevel
directory of the DAOS project on github,
in which the HTML documents for the static website are created.
To prevent this temporary subdirectory to be accidentally committed
into github, the
[.gitignore](https://github.com/daos-stack/daos/blob/master/.gitignore)
file contains a `site/` line.

### Mike configuration

The `mike` plugin is used with
[Materials for MkDocs](https://squidfunk.github.io/mkdocs-material/setup/setting-up-versioning/)
to generate versioned documentation for each of the DAOS release branches.
Its configuration information is stored in the `versions.json` file
that resides in a new `gh-pages` branch in a checked out version
of the DAOS repository.
Note that this file is **not** committed into the main DAOS-Stack
github repo. It only exists in the local `gh-pages` branch
on the system on which the webpages are generated.

The `versions.json` file should **not** be edited directly.
The `mike` command should be used to define which versions of the
documentation are being created, which aliases should be set up,
and which version should be the default version to display.
See below for details.

### Doxygen configuration

The configuration file for the `doxygen` tool is
[Doxyfile](https://github.com/daos-stack/daos/blob/master/Doxyfile),
located in the toplevel directory of the DAOS project on github.

## Installing the mkdocs Software

On the machine where the DAOS documentation is to be built,
GIT needs to be set up so the `daos-stack/daos` project
can be checked out.
In addition, the MkDocs package and plugins need to be installed.
Running `pip install mike` should install the prerequisite packages.

Depending on the operation system and Python environment,
prerquisite software and the mkdocs plugins may have to be explicitly
installed. For example, on a Windows laptop running Cygwin:

```
pip3 install alabaster sphinxcontrib-applehelp sphinxcontrib-devhelp sphinxcontrib-htmlhelp sphinxcontrib-jsmath sphinxcontrib-qthelp

pip3 install --user mkdocs
pip3 install --user mkdocs-material
pip3 install --user mike
export PATH="$HOME/.local/bin:$PATH"
mike --version
```

## Checking out DAOS from github

To create the DAOS documentation webpages, it is recommended to work
on a clone of the DAOS project on github that is **separate** from any
checked out version that may be used for code development work.

```
cd ~/dev
git clone git@github.com:daos-stack/daos.git daos-website
cd daos-website
git branch           # should be on master branch
grep site .gitignore # should show "site/"
grep doxy .gitignore # should show "docs/doxygen/"
mike list            # should show nothing on a new setup
```

## Creating the static website contents

To create the static website contents, the following process is used.
For each DAOS release branch that should get included on the website:

* the git release branch is checked out

* running `doxygen` creates a temporary version of the doxygen documentation
  for this release in the `docs/doxygen/html` subdirectory.

!!! note
To prevent these files from being accidentally committed into github,
the [.gitignore](https://github.com/daos-stack/daos/blob/master/.gitignore)
file contains a `docs/doxygen/` line.

* `mike deploy` is called with a title, a name for the release,
   and optional alias name(s) for this release.
   These will be used in the multi-versioned website's navigation system.

  - `mike` first uses `mkdocs` to create a temporary copy of the
    static webpages for this release in the `site/` subdirectory.

  - `mike` will then copy that `site/` contents into the temporary `gh-pages`
    branch, with a directory name that matches the release name
    specified on the `mike deploy` invocation.

  - Any aliases for that release will be created as symlinks in the
    `gh-pages` branch, pointing to the directory with the main release name.

This process is repeated for each release. After all versions have been
processed this way, invoking `mike default` will set the version that is
displayed on the website by default.
That command creates a toplevel `index.html` in the `gh-pages` branch which
contains a redirect to the version that was selected as the default.

A complete example for a website where DAOS 2.6 is the current "latest"
version, and the master branch is the "2.7" development branch for what
will eventually become DAOS 2.8:

```
mike delete --all

git checkout master
rm -rf docs/doxygen/html 2>/dev/null
doxygen
mike deploy -t "v2.7 - master" master v2.7 v2.8  2>&1 | tee deploy-log.master.txt
rm -rf docs/doxygen/html

git checkout release/2.6
rm -rf docs/doxygen/html 2>/dev/null
doxygen
mike deploy -t "v2.6 - latest" v2.6 latest  2>&1 | tee deploy-log.v2.6.txt
rm -rf docs/doxygen/html

git checkout release/2.4
rm -rf docs/doxygen/html 2>/dev/null
doxygen
mike deploy -t "v2.4 - deprecated" v2.4  2>&1 | tee deploy-log.v2.4.txt
rm -rf docs/doxygen/html

mike set-default latest
mike list
```

After `release/2.8` has been branched, but before it becomes generally
available (GA), the website version structure should be updated like this:

```
mike delete master
mike delete v2.7
mike delete v2.8

git checkout master
rm -rf docs/doxygen/html 2>/dev/null
doxygen
mike deploy -t "v2.9 - master" master v2.9 v3.0 2>&1 | tee deploy-log.master.txt
rm -rf docs/doxygen/html

git checkout release/2.8
rm -rf docs/doxygen/html 2>/dev/null
doxygen
mike deploy -t "v2.8 - rc" v2.8 rc  2>&1 | tee deploy-log.v2.8.txt
rm -rf docs/doxygen/html

mike list
```

And finally, when DAOS version 2.8 is released this will be set
as the new `latest` release:

```
mike delete rc
mike delete latest

git checkout release/2.8
rm -rf docs/doxygen/html 2>/dev/null
doxygen
mike deploy -t "v2.8 - latest" v2.8 latest 2>&1 | tee deploy-log.v2.8.txt
rm -rf docs/doxygen/html
mike set-default latest

mike list
```

It is useful to save the logs from the `mike deploy` invocation.
These logs will contain warnings about pages that exisit but are not
linked anywhere in the navigation structure, and other issues.

The new website can be tested locally by running `mike serve` on the machine
where the website is being created. This will start mike's built-in webserver.
The new contents can then be inspected by pointing a browser
(running on the same machine) to `http://localhost:8000/`.

## Staging the new website contents to docs.daos.io

Because the version aliases are symlinks, and because the website contents
consists of thousands of small files, copying it to the actual webserver
is best done by creating and transfering a tarfile, which can then be
un-tarred on the webserver and will keep the symlinks intact.

As a best practice, a new website version is first deployed in a
staging area so it can be validated in the same webserver environment
in which the live website is running:

```
export WEBSITE="docs.daos.io"
export TARFILE="docs-daos-io.tgz"

git checkout github-pages

tar czvf $TAR index.html v?.? latest master

ssh $WEBSERVER "rm -r docs.daos.io/staging && mkdir docs.daos.io/staging"
scp R$TARFILE $WEBSERVER:docs.daos.io/staging
ssh $WEBSERVER "cd docs.daos.io/staging && tar xz $TARFILE && rm $TARFILE"

rm $TARFILE

git checkout master
```

At this point, the new website can be inspected by using the
[https://docs.daos.io/staging/](https://docs.daos.io/staging/) URL.

## Go-live of the new website contents on docs.daos.io

To make the website in the staging area the active website, there's a small
`go-live.sh` script on the webserver that will archive the current live
version and replace it with the one in the staging area:

```
$ cat go-live.sh
D=`date "+%Y-%m-%d"`
mv docs.daos.io old-docs.daos.io.$D
mv old-docs.daos.io.$D/staging docs.daos.io
mkdir docs.daos.io/staging
```

## Miscellaneous tips and tricks

### Beware of relative URLs in links

The `mkdocs` tool creates a *directory* (with an `index.html` in it)
for each Markdown *file* that it processes.
So a documentation file like `docs/user/container.md`
in the github master branch will become this URL on the website:
[https://docs.daos.io/master/user/container/index.html](https://docs.daos.io/master/user/container/index.html).

This additional directory level may break relative URLs in
hyperlinks in the Markdown files. On the other hand, adding that
additional directory level to those relative URLs will break the
links when looking at the (un-processed) Markdown files directly
in github.

There is no good fix. In some places absolute paths to website URLs
are used to make sure those hyperlinks work in both the raw
Markdown and the rendered HTML pages on the website. The drawback
of this approach is that those URLs will contain hardcoded
release numbers, but those can be quickly updated by a
search and replace when a new release branch is created.

### Resolving Spell Checker Issues

The DAOS CI performs spell checking on PRs.
If the spell checker reports an error for a legitimate word
(or acronym), the error can be resolved by adding the term to the
wordlist that is maintained in the `utils/cq/words.dict` file.

### Always call mike on a release branch, not gh-pages

You must be in an actual DAOS release branch,
not in the `gh-pages` branch, to run mike commands.
Otherwise  the tool will report errors like this:

```
$ git branch
* gh-pages
  master

$ mike list
error: [Errno 2] No such file or directory: 'mkdocs.yml'; pass --config-file or set --remote/--branch explicitly
```

