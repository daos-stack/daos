#!/usr/bin/env python3
# Disable check for module name. disable-next doesn't work here.
# pylint: disable=invalid-name
# pylint: enable=invalid-name
"""Adds a warning message to the commit if a module is updated"""
import re
import subprocess  # nosec
import sys

modified_re = re.compile(r'^(?:M|A)(\s+)(?P<name>.*)')


def rebasing():
    """Determines if the current operation is a rebase"""
    with subprocess.Popen(["git", "branch"],
                          stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE) as process:

        stdout = process.communicate()[0].decode()
        return stdout.split('\n', maxsplit=1)[0].startswith("* (no branch, rebasing")


def submodule_check(modname, msg_file):
    """Determines if a sub-module has been updated"""
    modified = False

    with subprocess.Popen(['git', 'status', '--porcelain'], stdout=subprocess.PIPE) as proc:
        out, _ = proc.communicate()
        for line in out.decode().splitlines():
            match = modified_re.match(line)
            if match:
                modified = modified | (match.group('name') == modname)

        if not rebasing() and modified:
            with open(msg_file, 'r', encoding='utf-8') as file:
                lines = file.readlines()

            message = f'# WARNING *** This patch modifies the {modname} reference.  ' \
                      'Are you sure this is intended? *** WARNING'

            if lines[0] != message:
                lines = [message, "\n", "\n"] + lines

            with open(msg_file, 'w', encoding='utf-8') as file:
                file.writelines(lines)


def main(msg_file):
    """main"""
    for line in subprocess.check_output(['git', 'submodule',
                                        'status']).decode().rstrip().split('\n'):
        if line:
            submodule_check(line[1:].split(' ')[1], msg_file)


if __name__ == '__main__':

    main(sys.argv[1])
