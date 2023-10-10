# About DAOS Git hooks

Githooks are a [well documented](https://git-scm.com/docs/githooks) feature
of git that enable various local exectubles to be run during various stages of
the git workflow.

The DAOS repo contains several built-in githooks that are intended
to help developers conform to DAOS community coding standards and practices
and to avoid some common mistakes in the development cycle.

To enable these standard githooks requires a two step process:

1. Install the hooks

Configure your core.hookspath as follows (Recommended):

```sh
git config core.hookspath utils/githooks
```

Additionally, one can copy the files into an already configured path.

With the first option, any new githooks added to the repository will
automatically run, but possibly require additional software to produce the
desired effect.  Additionally, as the branch changes, the githooks
change with it.

2. Install all of the required tools

The Githooks framework in DAOS is such that the hooks will all run.
However, some hooks will simply check for required software and are
effectively a noop if such is not installed.

On many systems, the required packages can be installed through standard means
but customization may be required.  Some are specified in
[requirements.txt](../../requirements.txt) so can be installed using
`pip install -r requirements.txt` and `pip install -r utils/cq/requirements.txt`
which can also be done using a virtual environment. The following
packages are used by built-in githooks.

1. clang-format version 14.0.5 or higher.  If the check is unable to parse
the version output, it will fail.  Try running
`<root>/site_scons/site_tools/extra/extra.py` to check.
2. pylint
3. flake8
4. yamllint
5. gofmt

There is a daos wrapper around pylint at `utils/cq/daos_pylint.py` which will perform standard
pylint checks whilst managing scons and PYTHONPATH setup changes automatically.  Installing
the python packages in `utils/cq/requirements.txt` will allow it to test for all the python dependencies.

It is important to check the output on commit for any errors that may indicate
any one of the required tools is missing.

Additionally, [find_base.sh](find_base.sh) attempts to determine the base
branch using `gh`, the Github CLI. If this isn't installed, it will use
`master` as the base which can result in a larger diff and more files being
checked than expected.

## Checks performed by built-in scripts

### pre-commit

1. clang-format will update any C/C++ files changed using configuration in
[.clang-format](../../.clang-format).  If anything changed, it will exit,
allowing the user to inspect the changes and retry the commit.
2. pylint will check python changes and fail if there are errors.
3. flake8 will check python changes and fail if there are errors.
4. yamllint will check YAML file changes and fail if there are errors.
5. gofmt will check Go files changed and fail if there are errors.
6. Copyrights will be checked and updated if needed.

### prepare-commit-msg

1. Checks to see if any submodules have been updated in the patch and
inserts a warning message in a comment.  If the change is expected,
the comment can be ignored. If not, it's likely someone else changed
it and an update is needed before commit.  In such a case, abort the
commit by exiting without saving.

### commit-msg

1. Checks to see if githooks are installed locally and adds a watermark
to the commit message that is checked in CI.  It is not fatal but
gatekeepers may ask that githooks be used.

## Adding user specific hooks

The framework is extensible.  In order to add a custom user hook, a developer
simply must add an executable file using the following naming convention:

`utils/githooks/<hook>.d/<num>-user-<name>`

This pattern appears in [.gitignore](../../.gitignore) so such files cannot be
checked in. If such a file would be generically useful, however, consider
renaming it to remove `-user` and pushing a pull request and update this
document accordingly.
