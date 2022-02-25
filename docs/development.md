# Development

Please use the [pre-commit](https://pre-commit.com/) hooks configured in this repository to ensure that all Terraform modules are validated and properly documented before pushing code changes.


## Install pre-commit and dependencies

In order to use [pre-commit](https://pre-commit.com/) you will need to install it on your system.

You will also need to install the dependencies that are required for the pre-commit plugins used in this repository.

1. Install [pre-commit](https://pre-commit.com/).

   [pre-commit](https://pre-commit.com/) can be installed using standard package managers.

   Instructions can be found at the [pre-commit website](https://pre-commit.com/#install).


2. Install [TFLint](https://github.com/terraform-linters/tflint)

   See the [installation instructions](https://github.com/terraform-linters/tflint#installation)

   After installing tflint change into the root of the locally cloned git repo and run the `init` command.

   ```shell
   cd <root of google-cloud-daos repo>
   tflint --init
   ```

3. Install terraform-docs

   See [https://github.com/terraform-docs/terraform-docs](https://github.com/terraform-docs/terraform-docs)


4. MacOS only

   MacOS users will need to install `findutils` and `coreutils`.

   Before installing coreutils read the
   [gotchas about coreutils](https://www.pixelbeat.org/docs/coreutils-gotchas.html)
   to ensure that the installation will not negatively impact your
   system.

   **Homebrew**

   ```shell
   brew install findutils
   brew install coreutils
   ```

   **Conda**

   ```shell
   brew install findutils
   conda install coreutils
   ```

   Update your PATH  in your `~/.bashrc` or `~/.bash_profile`
   ```shell
   PATH="/usr/local/opt/coreutils/libexec/gnubin:$PATH"
   ```

## Install the pre-commit hook

After you have installed [pre-commit](https://pre-commit.com/) and its dependencies on your system you can need to install the pre-commit hook in
your local clone of the google-cloud-daos git repository.

```shell
cd <root of google-cloud-daos repo>
pre-commit install
```

## Running pre-commit

[pre-commit](https://pre-commit.com/) will now run on any files that are staged when you run `git commit -s`.

To run [pre-commit](https://pre-commit.com/) on all files prior to staging them

```shell
pre-commit run --all-files
```
