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


def update_commit_pragma(commit_message, pragma_values):
    """Update the existing commit pragma with new values.

    Inserts the updated commit pragmas after the commit message title.

    Args:
        commit_message (str): the original commit message
        pragma_values (dict): a dictionary of commit pragma name str keys and their set values. The
            commit pragma key will be removed for the commit message and, if it has values defined,
            a new entry will be added with the new values.

    Returns:
        str: the updated commit message
    """
    # Remove the old commit pragmas
    for pragma in sorted(pragma_values):
        regex = rf'^{pragma}:.*$'
        commit_message = re.sub(regex, '', commit_message, flags=re.MULTILINE | re.IGNORECASE)

    # Insert the new commit pragmas
    commit_message_split = commit_message.splitlines()
    insert_line = 1
    for pragma in sorted(pragma_values):
        if not pragma_values[pragma]:
            continue
        if insert_line == 1:
            commit_message_split.insert(insert_line, '')
            insert_line += 1
        commit_message_split.insert(
            insert_line, f'{pragma}: {" ".join(sorted(pragma_values[pragma]))}')
        insert_line += 1
    if insert_line > 1:
        commit_message_split.insert(insert_line, '')

    return os.linesep.join(commit_message_split)


def modify_commit_message_pragmas(commit_message):
    """Modify the commit message pragmas.

    Args:
        commit_message (str): the original commit message

    Returns:
        str: the modified commit message
    """
    tag_pragmas = {
        'Test-tag': set(),
        'Test-tag-hw-medium-md-on-ssd': set(),
        'Test-tag-hw-medium-verbs-provider-md-on-ssd': set(),
        'Test-tag-hw-large-md-on-ssd': set(),
        'Features': set(),
    }

    # Get the values for any tag pragma in the commit message
    for pragma, value_set in tag_pragmas.items():
        specified_values = get_pragma_values(commit_message, pragma)
        if specified_values:
            value_set.update(filter(None, specified_values.split(' ')))

    # Run the normally disabled Functional Hardware MD on SSD stages when they have tests
    for pragma in ('Test-tag-hw-medium-md-on-ssd',
                   'Test-tag-hw-medium-verbs-provider-md-on-ssd',
                   'Test-tag-hw-large-md-on-ssd'):
        if tag_pragmas[pragma]:
            skip_pragma = pragma.replace('Test-tag', 'Skip-func')
            skip_stage = get_pragma_values(commit_message, skip_pragma)
            if skip_stage != 'true':
                tag_pragmas[skip_pragma] = ['false']

    # Append any Features tags to the Test-tag pragma
    if tag_pragmas['Features']:
        tag_pragmas['Test-tag'].add('pr')
        for feature in tag_pragmas['Features']:
            tag_pragmas['Test-tag'].add(f'daily_regression,{feature}')
            tag_pragmas['Test-tag'].add(f'full_regression,{feature}')
        tag_pragmas['Features'].clear()

    # Update the commit message with the tag pragmas
    commit_message = update_commit_pragma(commit_message, tag_pragmas)

    return commit_message


def main():
    """Get the latest commit message with some modifications."""
    print(modify_commit_message_pragmas(git_commit_message()))


if __name__ == '__main__':
    main()
