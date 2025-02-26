#!/usr/bin/env python3
"""
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent

  Generate default commit pragmas.
"""

import importlib.util
import os
import subprocess  # nosec
from argparse import ArgumentParser

PARENT_DIR = os.path.dirname(__file__)


# Dynamically load the tags utility
tags_path = os.path.realpath(
    os.path.join(PARENT_DIR, '..', 'src', 'tests', 'ftest', 'tags.py'))
tags_spec = importlib.util.spec_from_file_location('tags', tags_path)
tags = importlib.util.module_from_spec(tags_spec)
tags_spec.loader.exec_module(tags)


def git_root_dir():
    """Get the git root directory.

    Returns:
        str: the root directory path
    """
    result = subprocess.run(
        ['git', 'rev-parse', '--show-toplevel'],
        stdout=subprocess.PIPE, check=True, cwd=PARENT_DIR)
    return result.stdout.decode().rstrip('\n')


def git_files_changed(target):
    """Get a list of files from git diff.

    Args:
        target (str): target branch or commit.

    Returns:
        list: absolute paths of modified files
    """
    git_root = git_root_dir()
    result = subprocess.run(
        ['git', 'diff', target, '--name-only', '--relative'],
        stdout=subprocess.PIPE, cwd=git_root, check=True)
    return [os.path.join(git_root, path) for path in result.stdout.decode().split('\n') if path]


def git_merge_base(*commits):
    """Run git merge-base.

    Args:
        commits (str): variable number of commit hashes

    Returns:
        str: the merge-base result
    """
    result = subprocess.run(
        ['git', 'merge-base', *commits],
        stdout=subprocess.PIPE, check=True, cwd=PARENT_DIR)
    return result.stdout.decode().rstrip('\n')


def gen_commit_pragmas(target):
    """Generate commit pragmas based on files modified.

    Currently generates:
        Test-tag

    Future enhancements could enable/disable stages as applicable.

    Args:
        target (str): git target to use as reference diff

    Returns:
        dict: pragmas and values
    """
    pragmas = {}

    # Don't bother if no files were modified.
    # E.g. this could be a timed build.
    modified_files = git_files_changed(target)
    if not modified_files:
        return pragmas

    test_tag = ' '.join(sorted(tags.files_to_tags(modified_files)))
    if test_tag:
        pragmas['Test-tag'] = test_tag

    return pragmas


def main():
    """Generate default commit pragmas."""
    parser = ArgumentParser()
    parser.add_argument(
        "--target",
        required=True,
        help="git target to as reference diff")
    args = parser.parse_args()

    commit_pragmas = gen_commit_pragmas(git_merge_base('HEAD', args.target))
    for pragma, value in commit_pragmas.items():
        print(f'{pragma}: {value}')


if __name__ == '__main__':
    main()
