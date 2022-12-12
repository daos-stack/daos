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

## Development Workflow and Release Process

Workflow for making changes to the [daos-stack/google-cloud-daos](https://github.com/daos-stack/google-cloud-daos) repo and updating the community examples in [GoogleCloudPlatform/hpc-toolkit](https://github.com/GoogleCloudPlatform/hpc-toolkit).

Since some of the Intel community examples [GoogleCloudPlatform/hpc-toolkit](https://github.com/GoogleCloudPlatform/hpc-toolkit) reference a specific tagged version of the DAOS Terraform modules in [daos-stack/google-cloud-daos](https://github.com/daos-stack/google-cloud-daos) we typically do not tag the [daos-stack/google-cloud-daos](https://github.com/daos-stack/google-cloud-daos) repo until we have tested the toolkit examples first.

This requires coordiation of changes between the two repos as described below.


### Changes to [daos-stack/google-cloud-daos](https://github.com/daos-stack/google-cloud-daos)

1. Fork [daos-stack/google-cloud-daos](https://github.com/daos-stack/google-cloud-daos) to your own GitHub account
2. Sync the `develop` branch in your forked repo
3. Clone your forked `google-cloud-daos` repo
4. Check out the `develop` branch
5. Create a new branch from the develop branch. The new branch should be named after the Jira ticket you are working on. Example: DAOSGCP-999
6. Modify code
7. Commit to your local dev branch
8. Push local dev branch to your forked repo
9. Submit PR from dev branch in your forked repo to develop branch in [daos-stack/google-cloud-daos](https://github.com/daos-stack/google-cloud-daos)
10. PR will be reviewed and merged when approved

### Changes to [GoogleCloudPlatform/hpc-toolkit](https://github.com/GoogleCloudPlatform/hpc-toolkit)

#### Overview

The [Google Cloud HPC Toolkit](https://github.com/GoogleCloudPlatform/hpc-toolkit) generates Terraform configurations that can be used to deploy HPC solutions on Google Cloud Platform.

The toolkit uses yaml files called [blueprints](https://cloud.google.com/hpc-toolkit/docs/setup/hpc-blueprint) to generate Terraform configurations.

The [community/examples/intel/daos-*.yaml](https://github.com/GoogleCloudPlatform/hpc-toolkit/tree/main/community/examples/intel) blueprint examples use a tagged version of the DAOS terraform modules in [daos-stack/google-cloud-daos/tree/main/terraform/modules](https://github.com/daos-stack/google-cloud-daos/tree/main/terraform/modules) to deploy DAOS instances.

When there are new tags on the main branch of [daos-stack/google-cloud-daos](https://github.com/daos-stack/google-cloud-daos) the [GoogleCloudPlatform/hpc-toolkit/community/examples/intel/daos-*.yaml](https://github.com/GoogleCloudPlatform/hpc-toolkit/tree/main/community/examples/intel) files must be updated accordingly.

Depending on the type of changes being made it may also be necessary to update the documentation in [community/modules/file-system/Intel-DAOS/README.md](https://github.com/GoogleCloudPlatform/hpc-toolkit/blob/main/community/modules/file-system/Intel-DAOS/README.md)

#### Updating the examples in [GoogleCloudPlatform/hpc-toolkit](https://github.com/GoogleCloudPlatform/hpc-toolkit)

1. Fork the [GoogleCloudPlatform/hpc-toolkit](https://github.com/GoogleCloudPlatform/hpc-toolkit) repo
2. Sync the develop branch of the [GoogleCloudPlatform/hpc-toolkit](https://github.com/GoogleCloudPlatform/hpc-toolkit) repo in your fork
3. Clone your forked hpc-toolkit repo
4. Check out the develop branch
5. Create a new dev branch from the develop branch. The new branch should be named after the Jira ticket you are working on. Example: DAOSGCP-999
6. Modify the [community/examples/intel/daos-*.yaml](https://github.com/GoogleCloudPlatform/hpc-toolkit/tree/main/community/examples/intel) blueprints to point to the develop branch of [daos-stack/google-cloud-daos](https://github.com/daos-stack/google-cloud-daos).
7. Run each blueprint example
8. Make changes to blueprints as necessary

After the example blueprints are working with the DAOS terraform modules in the tip of [daos-stack/google-cloud-daos](https://github.com/daos-stack/google-cloud-daos) you can then focus on releasing a new version of [daos-stack/google-cloud-daos](https://github.com/daos-stack/google-cloud-daos).

Set the toolkit aside for now.  You will need to come back to it after you release a new version of [daos-stack/google-cloud-daos](https://github.com/daos-stack/google-cloud-daos).


### Release new version of [daos-stack/google-cloud-daos](https://github.com/daos-stack/google-cloud-daos)

#### Prerequisites

- All PRs for a release have been merged to the `develop` branch in [daos-stack/google-cloud-daos](https://github.com/daos-stack/google-cloud-daos)
- The examples and documentation in the following locations have been tested against the tip of `develop` branch in [daos-stack/google-cloud-daos](https://github.com/daos-stack/google-cloud-daos)
  - [GoogleCloudPlatform/hpc-toolkit/tree/main/community/examples/intel/daos-*.yaml](https://github.com/GoogleCloudPlatform/hpc-toolkit/tree/main/community/examples/intel)
  - [GoogleCloudPlatform/hpc-toolkit/tree/main/community/modules/file-system/Intel-DAOS/READ.md](https://github.com/GoogleCloudPlatform/hpc-toolkit/tree/main/community/modules/file-system/Intel-DAOS/READ.md)

#### Release

1. Create release notes. See a [previous release](https://github.com/daos-stack/google-cloud-daos/releases/) for format and content.
2. Create a JIRA ticket to track the task of merging from develop to main
3. Create a PR to merge develop branch to main branch in [daos-stack/google-cloud-daos](https://github.com/daos-stack/google-cloud-daos). Use the Jira ticket number in the title of the PR.
4. Merge PR
5. Tag main branch with new version tag and update release notes.


### Update [GoogleCloudPlatform/hpc-toolkit](https://github.com/GoogleCloudPlatform/hpc-toolkit) to use new version of DAOS Terraform modules

1. Modify the URLs in all [daos-*.yaml](https://github.com/GoogleCloudPlatform/hpc-toolkit/tree/main/community/examples/intel) blueprints.  URLs need to point to the latest tagged version of the DAOS modules in the `main` branch.
2. Run each blueprint to test
3. Commit changes to local dev branch
4. Push local dev branch to forked `hpc-toolkit` repo
5. Submit PR from dev branch in your fork to the develop branch in [GoogleCloudPlatform/hpc-toolkit](https://github.com/GoogleCloudPlatform/hpc-toolkit)
