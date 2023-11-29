#!/usr/bin/env python3
"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import re
import subprocess  # nosec

PARENT_DIR = os.path.dirname(__file__)


def git_commit_message():
    """Get the latest git commit message.

    Returns:
        str: the commit message
    """
    result = subprocess.run(
        ['git', 'show', '-s', '--format=%B'],
        stdout=subprocess.PIPE, check=True, cwd=PARENT_DIR)
    return result.stdout.decode().rstrip('\n')


def get_pragma_values(commit_message, pragma):
    """Get the commit pragma values.

    Supports multiple pragma entries in the commit message.

    Args:
        commit_message (str): the commit message
        pragma (str): the commit pragma name (case-sensitive)

    Returns:
        str: the values defined for the specified pragma
    """
    regex = rf'^{pragma}:\s*(.*)$'
    return ' '.join(re.findall(regex, commit_message, flags=re.MULTILINE | re.IGNORECASE))


def update_commit_pragma(commit_message, pragma, new_values):
    """Update the existing commit pragma with new values.

    Inserts the updated commit pragmas after the commit message title.

    Args:
        commit_message (str): the original commit message
        pragma (str): the commit pragma name
        new_values (str): values to assign for the new commit pragma. If None the old commit pragma
            is simply removed.

    Returns:
        str: the updated commit message
    """
    regex = rf'^{pragma}:.*$'

    # Remove the old commit pragma
    commit_message = re.sub(regex, '', commit_message, flags=re.MULTILINE | re.IGNORECASE)
    if new_values is None:
        return commit_message

    # Insert the new commit pragma
    commit_message_split = commit_message.splitlines()
    commit_message_split.insert(1, '')
    commit_message_split.insert(2, f'{pragma}: {new_values}')
    commit_message_split.insert(3, '')
    return os.linesep.join(commit_message_split)


def modify_commit_message_pragmas(commit_message):
    """Modify the commit message pragmas.

    Args:
        commit_message (str): the original commit message

    Returns:
        str: the modified commit message
    """
    test_tags = set()
    test_tags_md_on_ssd = 'pr,md_on_ssd'
    test_tag_values = get_pragma_values(commit_message, 'Test-tag')
    if test_tag_values:
        # When 'Test-tags:' exists in the commit message use the same tags for the MD on SSD testing
        test_tags.update(filter(None, test_tag_values.split(' ')))
        test_tags_md_on_ssd = test_tag_values
    else:
        # Default to using the 'pr' tag
        test_tags.add('pr')

    feature_values = get_pragma_values(commit_message, 'Features')
    if feature_values:
        # Add daily and weekly feature tests when 'Features' exists
        for feature in filter(None, feature_values.split(' ')):
            test_tags.add(f'daily_regression,{feature}')
            test_tags.add(f'full_regression,{feature}')

    # Replace 'Features:' with 'Test-tag:'
    commit_message = update_commit_pragma(commit_message, 'Features', None)
    commit_message = update_commit_pragma(commit_message, 'Test-tag', ' '.join(test_tags))

    if 'Test-tag-hw-medium-md-on-ssd' not in commit_message:
        # Add tags for MD on SSD testing
        commit_message = update_commit_pragma(
            commit_message, 'Test-tag-hw-medium-md-on-ssd', test_tags_md_on_ssd)

    return commit_message


def main():
    """Get the latest commit message with some modifications."""
    print(modify_commit_message_pragmas(git_commit_message()))


if __name__ == '__main__':
    main()
