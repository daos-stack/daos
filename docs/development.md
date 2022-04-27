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

4. Add `ADDLICENSE_COMPANY_NAME` environment variable to your `~/.bashrc` file

   When pre-commit runs for the first time it will download the [google/addlicense](https://github.com/google/addlicense/releases/tag/v1.0.0) binary into the `tools/autodoc/` directory. The `addlicense` binary is excluded in the `.gitignore` file so it does not get checked into the repo.

   The `addlicense` pre-commit hook will ensure that files have the proper license header.

   The company name that is used in the license header is specified in the `ADDLICENSE_COMPANY_NAME` environment variable.

   If the `ADDLICENSE_COMPANY_NAME` environment variable is not present, the company name in the license header will be set to **Intel Corporation**

   If you do not work for Intel be sure to export the `ADDLICENSE_COMPANY_NAME` environment variable with the name of your company as it should appear in the license header of files.

   ```bash
   export ADDLICENSE_COMPANY_NAME="your_company_name_here"
   ```

5. MacOS only

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

## Updating Cloud Shell urls in documentation

Several of the README.md files in this repository contain links that open tutorials in Cloud Shell.

In order for these links to work properly during development the URLs must be changed to point to the correct branch.

Currently Cloud Shell tutorials do not have an automatic way to detect a branch.  Therefore, the branch parameter in the URL must be updated manually.

The `tools/autodoc/cloudshell_urls.sh` script should be used to update the branch parameter in all Cloud Shell URLs that are present in *.md files in this repo.

### Update Cloud Shell URLs when submitting a PR

If your PR changes README.md files that contain Cloud Shell URLs, then prior to requesting a review you should run the following command and push any changes to your dev branch.

```bash
tools/autodoc/cloudshell_urls.sh --repo-url <your_forked_repo_url> --branch <your_dev_branch_name>
```

This will allow the reviewers to run Cloud Shell tutorials from your PR branch.

### Update Cloud Shell URLs before merging to a branch

If you are merging changes to `*.md` files with Cloud Shell URLs in them you need to ensure that the URLs are updated with the name of the target branch before you merge.

This is not ideal but it's the only way we can think of doing things for now.

Let's say that you have a PR that has been approved and you want to merge it to the `develop` branch.

Prior to merging you need to run

```bash
tools/autodoc/cloudshell_urls.sh --repo-url <your_forked_repo_url> --branch develop
```

And then commit the changes in your dev branch.

Once that is done you can then merge to the `develop` branch.

Now let's say that you want to merge the `develop` branch into the `main` branch.

You will need to check out the https://github.com/daos-stack/google-cloud-daos `develop` branch and run

```bash
tools/autodoc/cloudshell_urls.sh --repo-url https://github.com/daos-stack/google-cloud-daos --branch main
```

Commit the changes and push them to the develop branch. After doing this you can merge the `develop` branch to `main`.

Now you will need to set the URLs back to the develop branch.

```bash
tools/autodoc/cloudshell_urls.sh --repo-url https://github.com/daos-stack/google-cloud-daos --branch develop
```

Commit the changes and push them to the develop branch.

This is very tedious. We will continue to seek out a better solution for maintaining the Cloud Shell URLs.
