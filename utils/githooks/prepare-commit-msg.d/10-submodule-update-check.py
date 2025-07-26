#!/usr/bin/env python3
# Disable check for module name. disable-next doesn't work here.
# pylint: disable=invalid-name
# pylint: enable=invalid-name
"""Adds a warning message to the commit if a module is updated"""
import re
import subprocess  # nosec
import sys


def rebasing():
    """Determines if the current operation is a rebase"""
    with subprocess.Popen(["git", "branch"],
                          stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE) as process:

        stdout = process.communicate()[0].decode()
        return stdout.split('\n', maxsplit=1)[0].startswith("* (no branch, rebasing")


def git_submodules():
    """Get a list of submodules"""
    lines = subprocess.check_output(['git', 'submodule', 'status']).decode().rstrip().split('\n')
    return [
        line[1:].split(' ')[1]
        for line in lines
        if line
    ]


def git_modified_files():
    """Get a list of modified files"""
    modified_re = re.compile(r'^(?:M|A)(\s+)(?P<name>.*)')
    files = []
    with subprocess.Popen(['git', 'status', '--porcelain'], stdout=subprocess.PIPE) as proc:
        out, _ = proc.communicate()
        for line in out.decode().splitlines():
            match = modified_re.match(line)
            if match:
                files.append(match.group('name'))
    return files


def main(msg_file):
    """main"""
    # Ignore if this is a rebase
    if rebasing():
        return

    modified_files = git_modified_files()
    submodules = git_submodules()

    messages = []
    for modified_submodule in set(modified_files).intersection(submodules):
        messages.append(
            f'# WARNING *** This patch modifies the {modified_submodule} reference. '
            'Are you sure this is intended? *** WARNING\n\n')

    if messages:
        with open(msg_file, 'r', encoding='utf-8') as file:
            lines = file.readlines()

        lines = messages + lines

        with open(msg_file, 'w', encoding='utf-8') as file:
            file.writelines(lines)


if __name__ == '__main__':
    main(sys.argv[1])
