#!/usr/bin/env python3
"""
  (C) Copyright 2018-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent

  Generate default commit pragmas.
"""

import importlib.util
import os
import re
import subprocess  # nosec
from argparse import ArgumentParser

import yaml

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


def read_commit_pragma_mapping():
    """Read commit_pragma_mapping.yaml.

    Also validates the config as read.

    Returns:
        dict: the config

    Raises:
        TypeError: If config types are invalid
        ValueError: If config values are invalid
    """
    with open(os.path.join(PARENT_DIR, 'commit_pragma_mapping.yaml'), 'r') as file:
        config = yaml.safe_load(file.read())

    for path_match, path_config in config.items():
        for pragma_key, pragma_config in path_config.items():
            if pragma_key == 'test-tag':
                _type = type(pragma_config)
                if _type not in (str, dict):
                    raise TypeError(f'Expected {str} or {dict}, not {_type}')
                if _type == str:
                    # Direct, no further checking needed
                    continue

                # Check for invalid config options
                extra_keys = set(config[path_match][pragma_key].keys()) - \
                    set(['tags', 'handler', 'stop_on_match'])
                if extra_keys:
                    raise ValueError(f'Unsupported keys in config: {", ".join(extra_keys)}')

                # Set default handler and check for invalid values
                if 'handler' not in config[path_match][pragma_key]:
                    config[path_match][pragma_key]['handler'] = 'direct'
                elif config[path_match][pragma_key]['handler'] not in ('direct', 'FtestTagMap'):
                    raise ValueError(
                        f'Invalid handler: {config[path_match][pragma_key]["handler"]}')

                # Set default stop_on_match and check for invalid values
                if 'stop_on_match' not in config[path_match][pragma_key]:
                    config[path_match][pragma_key]['stop_on_match'] = False
                elif not isinstance(config[path_match][pragma_key]['stop_on_match'], bool):
                    raise ValueError(
                        f'Invalid stop_on_match: {config[path_match][pragma_key]["stop_on_match"]}')

                # Check for missing or invalid tags
                if 'tags' not in config[path_match][pragma_key]:
                    raise ValueError(f'Missing tags for {path_match}')
                if not isinstance(config[path_match][pragma_key]['tags'], str):
                    raise TypeError(
                        f'Expected {str} for tags, not '
                        f'{type(config[path_match][pragma_key]["tags"])}')
                continue
            raise ValueError(f'Invalid pragma key: {pragma_key}')

    return config


def __get_test_tag(test_tag_config, paths):
    """Get the Test-tag pragma.

    Args:
        test_tag_config (dict): test-tag config. E.g. {path1: foo, path2: {tags: pr}}
        paths (list): paths to get tags for

    Returns:
        str: Test-tag pragma for these paths
    """
    # Get tags for ftest paths
    ftest_tag_map = tags.FtestTagMap(tags.all_ftest_python_files())

    all_tags = set()

    for path in paths:
        for _pattern, _config in test_tag_config.items():
            if re.search(rf'{_pattern}', path):
                if isinstance(_config, str):
                    _config = {
                        'tags': _config,
                        'handler': 'direct',
                        'stop_on_match': False
                    }
                _handler = _config['handler']
                if _handler == 'FtestTagMap':
                    # Special ftest handling
                    try:
                        all_tags.update(ftest_tag_map.minimal_tags(path))
                    except KeyError:
                        # Use default from config
                        all_tags.update(_config['tags'].split(' '))
                elif _handler == 'direct':
                    # Use direct tags from config
                    all_tags.update(_config['tags'].split(' '))
                else:
                    raise ValueError(f'Invalid handler: {_config["handler"]}')

                if _config['stop_on_match']:
                    # Don't process further entries for this path
                    break

    return ' '.join(sorted(all_tags))


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

    commit_pragma_mapping = read_commit_pragma_mapping()

    def __pragma_config(pragma_key, default):
        """Return the configs for a single pragma key.

        For example:
            {path: test-tag: tag_config}
        For pragma_key == 'test-tag' returns:
            {path: tag_config}
        """
        return {
            path_match: path_config.get(pragma_key, default)
            for path_match, path_config in commit_pragma_mapping.items()
        }

    test_tag = __get_test_tag(__pragma_config('test-tag', 'pr'), modified_files)
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
