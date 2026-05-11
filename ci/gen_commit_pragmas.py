#!/usr/bin/env python3
"""
  (C) Copyright 2018-2024 Intel Corporation.
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

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

                # Check for missing or invalid tags
                if 'tags' not in config[path_match][pragma_key]:
                    raise ValueError(f'Missing tags for {path_match}')
                if not isinstance(config[path_match][pragma_key]['tags'], str):
                    raise TypeError(
                        f'Expected {str} for tags, not '
                        f'{type(config[path_match][pragma_key]["tags"])}')
            elif pragma_key == 'need-unit-test':
                _type = type(pragma_config)
                if _type not in (bool, dict):
                    raise TypeError(f'Expected {bool} or {dict}, not {_type}')
                if _type == bool:
                    # Direct, no further checking needed
                    continue

                # Check for invalid config options
                extra_keys = set(config[path_match][pragma_key].keys()) - \
                    set(['val', 'stop_on_match'])
                if extra_keys:
                    raise ValueError(f'Unsupported keys in config: {", ".join(extra_keys)}')

                # Check for missing or invalid val
                if 'val' not in config[path_match][pragma_key]:
                    raise ValueError(f'Missing val for {path_match}')
                if not isinstance(config[path_match][pragma_key]['val'], bool):
                    raise TypeError(
                        f'Expected {bool} for val, not '
                        f'{type(config[path_match][pragma_key]["val"])}')
            elif pragma_key == 'stop_on_match':
                if not isinstance(pragma_config, bool):
                    raise TypeError(f'Expected {bool} for stop_on_match, not {type(pragma_config)}')
            else:
                raise ValueError(f'Invalid pragma key: {pragma_key}')

    return config


def __get_test_tag(commit_pragma_mapping, paths, default='pr'):
    """Get the Test-tag pragma.

    Args:
        commit_pragma_mapping (dict): full commit pragma mapping config
        paths (list): paths to get tags for
        default (str): default tag to use if a path does not have a test-tag config

    Returns:
        str: Test-tag pragma for these paths
    """
    # Get tags for ftest paths
    ftest_tag_map = tags.FtestTagMap(tags.all_ftest_python_files())

    all_tags = set()

    for path in paths:
        for _pattern, config in commit_pragma_mapping.items():
            if re.search(rf'{_pattern}', path):
                test_tag_config = config.get('test-tag', default)
                if isinstance(test_tag_config, str):
                    test_tag_config = {
                        'tags': test_tag_config,
                        'handler': 'direct'
                    }
                _handler = test_tag_config['handler']
                if _handler == 'FtestTagMap':
                    # Special ftest handling
                    try:
                        all_tags.update(ftest_tag_map.minimal_tags(path))
                    except KeyError:
                        # Use default from config
                        all_tags.update(test_tag_config['tags'].split(' '))
                elif _handler == 'direct':
                    # Use direct tags from config
                    all_tags.update(test_tag_config['tags'].split(' '))
                else:
                    raise ValueError(f'Invalid handler: {test_tag_config["handler"]}')

                if test_tag_config.get('stop_on_match', config.get('stop_on_match', False)):
                    # Don't process further entries for this path
                    break

    # Make sure the test-tag we get from the config are valid tags in ftest
    ftest_unique_tags = ftest_tag_map.unique_tags()
    invalid_tags = all_tags - ftest_unique_tags
    if invalid_tags:
        raise ValueError(f'test-tag does not match any tests: {", ".join(invalid_tags)}')

    return ' '.join(sorted(all_tags))


def __get_need_unit_test(commit_pragma_mapping, paths, default=True):
    """Determine whether we need to run unit tests for these paths.

    Args:
        commit_pragma_mapping (dict): full commit pragma mapping config
        paths (list): paths to match on
        default (bool): default value to use if a path does not have a need-unit-test config

    Returns:
        bool: whether we need to run unit tests for these paths
    """
    for path in paths:
        for _pattern, config in commit_pragma_mapping.items():
            if re.search(rf'{_pattern}', path):
                unit_test_config = config.get('need-unit-test', default)
                if isinstance(unit_test_config, bool):
                    unit_test_config = {
                        'val': unit_test_config
                    }
                if unit_test_config['val']:
                    # If any path matches with a True value, we need to run unit tests
                    return True

                if unit_test_config.get('stop_on_match', config.get('stop_on_match', False)):
                    # Don't process further entries for this path
                    break

    return False


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

    test_tag = __get_test_tag(commit_pragma_mapping, modified_files)
    if test_tag:
        pragmas['Test-tag'] = test_tag

    need_unit_test = __get_need_unit_test(commit_pragma_mapping, modified_files)
    if modified_files and not need_unit_test:
        pragmas['Skip-unit-tests'] = True
        pragmas['Skip-fault-injection-test'] = True

    return pragmas


def main():
    """Generate default commit pragmas."""
    parser = ArgumentParser()
    parser.add_argument(
        "--target",
        required=True,
        help="git target to use as reference diff")
    args = parser.parse_args()

    commit_pragmas = gen_commit_pragmas(git_merge_base('HEAD', args.target))
    for pragma, value in commit_pragmas.items():
        print(f'{pragma}: {value}')


if __name__ == '__main__':
    main()
