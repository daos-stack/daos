#!/usr/bin/env python3
"""Get the latest commit message with some modifications."""

import importlib.util
import os
import re
import subprocess  # nosec
from argparse import ArgumentParser

PARENT_DIR = os.path.dirname(__file__)


# Dynamically load the tags
tags_path = os.path.realpath(
    os.path.join(PARENT_DIR, '..', 'src', 'tests', 'ftest', 'tags.py'))
tags_spec = importlib.util.spec_from_file_location('tags', tags_path)
tags = importlib.util.module_from_spec(tags_spec)
tags_spec.loader.exec_module(tags)


def git_commit_message():
    """Get the latest git commit message.

    Returns:
        str: the commit message
    """
    result = subprocess.run(
        ['git', 'show', '-s', '--format=%B'],
        stdout=subprocess.PIPE, check=True, cwd=PARENT_DIR)
    return result.stdout.decode().rstrip('\n')


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


def modify_commit_message_pragmas(commit_message, target):
    """Modify the commit message pragmas.

    TODO: if a commit already has Test-tag, do not overwrite. Just comment the suggested.

    Args:
        commit_message (str): the original commit message
        target (str): where the current branch is intended to be merged

    Returns:
        str: the modified commit message
    """
    modified_files = git_files_changed(target)

    rec_tags = tags.files_to_tags(modified_files)

    # Extract all "Features" and "Test-tag"
    feature_tags = re.findall(
        r'^Features:(.*)$', commit_message, flags=re.MULTILINE | re.IGNORECASE)
    if feature_tags:
        for _tags in feature_tags:
            rec_tags.update(filter(None, _tags.split(' ')))
        commit_message = re.sub(
            r'^Features:.*$', '', commit_message, flags=re.MULTILINE | re.IGNORECASE)
    test_tags = re.findall(
        r'^Test-tag:(.*)$', commit_message, flags=re.MULTILINE | re.IGNORECASE)
    if test_tags:
        for _tags in test_tags:
            rec_tags.update(filter(None, _tags.split(' ')))
        commit_message = re.sub(
            r'^Test-tag:.*$', '', commit_message, flags=re.MULTILINE | re.IGNORECASE)

    # Put "Test-tag" after the title
    commit_message_split = commit_message.splitlines()
    commit_message_split.insert(1, '')
    commit_message_split.insert(2, '# Auto-recommended Test-tag')
    commit_message_split.insert(3, f'Test-tag: {" ".join(sorted(rec_tags))}')
    return os.linesep.join(commit_message_split)


def main():
    parser = ArgumentParser()
    parser.add_argument(
        "--target",
        help="if given, used to modify commit pragmas")
    args = parser.parse_args()

    if args.target:
        print(modify_commit_message_pragmas(git_commit_message(), args.target))
    else:
        print(git_commit_message())


if __name__ == '__main__':
    main()
