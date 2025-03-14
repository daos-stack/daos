# About DAOS Git hooks

Githooks are a [well documented](https://git-scm.com/docs/githooks) feature
of git that enable various local executables to be run during various stages of
the git workflow.

The DAOS repo contains several built-in githooks that are intended
to help developers conform to DAOS community coding standards and practices
and to avoid some common mistakes in the development cycle.

## Install DAOS Git Hooks

Installing is a two-step process:

### 1. Install the hooks

Recommended: Configure your `core.hookspath`.  
Any new githooks added to the repository will automatically run,
but possibly require additional software to produce the desired effect.
Additionally, as the branch changes, the githooks change with it.

```sh
git config core.hookspath utils/githooks
```

Additionally, one can copy the files into an already configured path.

### 2. Install the tools

The Githooks framework in DAOS is such that the hooks will all run.
However, some hooks will simply check for required software and are
effectively a noop if such is not installed.

Requirements come from a combination of `pip` and system packages and can usually be installed through standard means.  
To install `pip` packages specified in [utils/cq/requirements.txt](../../utils/cq/requirements.txt) it is recommended to setup a virtual environment and install with pip.  
If you already have a [virtual environment for building](../../docs/QSG/build_from_scratch.md#python-packages) you can simply install the requirements:

```sh
python3 -m pip install -r utils/cq/requirements.txt
```

Install system packages with your package manager - for example:

```sh
sudo dnf install git-clang-format -y
```

#### Installed tools

The following packages are used by built-in githooks.

1. `git-clang-format` and `clang-format` version 14.0.5 or higher - Formatter for C code.
If the check is unable to parse the version output, it will fail. Try running
`<root>/site_scons/site_tools/extra/extra.py` to check.
2. `pylint`
3. `flake8`
4. `isort`
5. `yamllint`
6. `gofmt` or `golang-go`
7. `codespell`
8. `scons`
9. `gh`

#### Optional tools

Additionally, [find_base.sh](find_base.sh) attempts to determine the base
branch using `gh`, the Github CLI. If this isn't installed, it will use
`master` or a `release` branch as the base, which can result in a larger diff and more files being
checked than expected.

## Checks performed by built-in scripts

### pre-commit

It is important to check the output on commit for any errors that may indicate
any one of the required tools is missing.

1. update-copyright - Custom tool that automatically updates copyrights in modified files.
2. codespell - Linter for spelling mistakes in modified files
   - See [codespell.ignores](../../ci/codespell.ignores) for ignored words.
   - See [words.dict](../../utils/cq/words.dict) for words added to the DAOS project.
3. Jenkinsfile - Custom linter if the Jenkinsfile is modified
4. yamllint - Linter for modified YAML configs
5. clang-format - Automatically formats for C/C++ files modified. If anything changed it will exit,
allowing the user to inspect the changes and retry the commit.
   - See [.clang-format](../../.clang-format) for configuration
   - In some cases unwanted formatting changes are made. To disable formatting, for example:

     ```c
     /* clang-format off */
     ...
     /* clang-format on */
     ```

6. gofmt - Automatically formats for modified GO files
7. isort - Linter for python imports on modified python files
8. flake - Linter for python files
9. pylint - Additional linter for modified python files
   - See [daos_pylint.py](../../utils/cq/daos_pylint.py) for a custom wrapper around `pylint` which manages `PYTHONPATH` setup internally.
10. ftest - Custom linter for modified ftest files

### prepare-commit-msg

1. Checks to see if any submodules have been updated in the patch and
inserts a warning message in a comment.  If the change is expected,
the comment can be ignored. If not, it's likely someone else changed
it and an update is needed before commit.  In such a case, abort the
commit by exiting without saving.

## Adding user specific hooks

The framework is extensible.  In order to add a custom user hook, a developer
simply must add an executable file using the following naming convention:

`utils/githooks/<hook>.d/<num>-user-<name>`

This pattern appears in [.gitignore](../../.gitignore) so such files cannot be
checked in. If such a file would be generically useful, however, consider
renaming it to remove `-user` and pushing a pull request and update this
document accordingly.
