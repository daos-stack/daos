#!/usr/bin/env python3
"""
  (C) Copyright 2022-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import getpass
import logging
import os
import sys
import tempfile
import traceback
from argparse import ArgumentParser
from functools import partial
from itertools import product
from multiprocessing import Process, Queue  # MUST be a multiprocessing Queue

# pylint: disable=import-error,no-name-in-module
from util.logger_utils import get_console_handler
from util.run_utils import run_local
from util.user_utils import get_user_uid_gid

# Set up a logger for the console messages
logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)
logger.addHandler(get_console_handler("%(message)s", logging.INFO))


class VerifyPermsError(Exception):
    '''Base Exception class'''


def create(path, entry_type, owner=None):
    '''Create an entry.

    Args:
        path (str): path to create
        entry_type (str): file or dir
        owner (str, optional): user to create as. Defaults to current user

    Raises:
        ValueError: on invalid input

    '''
    if entry_type not in ('file', 'dir'):
        raise ValueError(f'Invalid entry_type: {entry_type}')
    if os.path.exists(path):
        raise ValueError(f'Path already exists: {path}')
    logger.info('Creating %s %s', entry_type, path)
    if owner:
        owner_uid_gid = get_user_uid_gid(owner)
        _as_user(owner_uid_gid, _create, path, entry_type)
    else:
        _create(path, entry_type)


def _create(path, entry_type):
    '''Create a file or directory.

    Files are created with a shebang to be executable.

    Args:
        path (str): path to create
        entry_type (str): file or dir

    '''
    if entry_type == 'file':
        with open(path, 'w', encoding='utf-8') as file:
            # Allow the file to be executable
            file.write('#!/usr/bin/env bash\n')
    elif entry_type == 'dir':
        os.mkdir(path)


def delete(path, owner=None):
    '''Delete a path.

    Args:
        path (str): path to delete
        owner (str, optional): user to delete as. Defaults to current user

    '''
    if not os.path.exists(path):
        return
    logger.info('Removing %s', path)
    if owner:
        owner_uid_gid = get_user_uid_gid(owner)
        _as_user(owner_uid_gid, _delete, path)
    else:
        _delete(path)


def _delete(path):
    '''Delete a path.

    Args:
        path (str): path to delete

    '''
    if os.path.isdir(path):
        os.rmdir(path)
    else:
        os.remove(path)


def verify(path, perms, owner=None, group_user=None, other_user=None, verify_mode='simple',
           do_chmod=True):
    '''Verify rwx permissions.

    Args:
        path (str): path to verify
        perms (list): octal or rwx permissions to verify
        owner (str, optional): user to run chmod as and verify "owner" permissions.
            Defaults to current user
        group_user (str, optional): user to verify "group" permissions for. Defaults to None
        other_user (str, optional): user to verify "other" permissions for. Defaults to None
        verify_mode (str, optional): mode of permission checks. Either 'simple' or 'real'.
            Default is 'simple'.
        do_chmod (bool, optional): whether to chmod the entry before verifying permissions.
            Must be True when verifying multiple permission sets. Default is True.

    Raises:
        ValueError: on invalid input
        FileNotFoundError: if a path does not exist
        VerifyPermsError: if permissions are not as expected

    '''
    owner = owner or getpass.getuser()
    if verify_mode not in ('simple', 'real'):
        raise ValueError(f'Invalid verify_mode: {verify_mode}')
    parsed_perms = _parse_perms(perms)
    if not do_chmod and len(parsed_perms) > 1:
        raise ValueError('do_chmod must be True when verifying multiple permission sets')

    # Map each user to their uid and gid
    owner_uid_gid = get_user_uid_gid(owner)
    group_uid_gid = get_user_uid_gid(group_user) if group_user else None
    other_uid_gid = get_user_uid_gid(other_user) if other_user else None

    path = os.path.realpath(path)
    if os.path.isfile(path):
        entry_type = 'file'
    elif os.path.isdir(path):
        entry_type = 'dir'
    elif os.path.exists(path):
        raise ValueError(f'Invalid entry type: {entry_type}')
    else:
        raise FileNotFoundError(f'Not found: {path}')

    logger.info('Verifying %s', path)
    for perm in parsed_perms:
        # chmod as the owner
        logger.info('  with perms %s', perm)
        if do_chmod:
            _as_user(owner_uid_gid, os.chmod, path, int(perm, base=8))

        # verify as owner
        logger.info('    as user %s, perm %s', owner, perm[0])
        _as_user(owner_uid_gid, _verify_one, path, entry_type, perm[0], verify_mode)

        # Verify as group
        if group_user:
            logger.info('    as user %s, perm %s', group_user, perm[1])
            _as_user(group_uid_gid, _verify_one, path, entry_type, perm[1], verify_mode)

        # Verify as other
        if other_user:
            logger.info('    as user %s, perm %s', other_user, perm[2])
            _as_user(other_uid_gid, _verify_one, path, entry_type, perm[2], verify_mode)


def _verify_one(path, entry_type, user_perm, verify_mode):
    '''verify() helper to verify a permission.

    Args:
        path (str): the path to verify
        entry_type (str): file or dir
        user_perm: (str): octal permissions for a single user
        verify_mode (str): real or simple

    Raises:
        VerifyPermsError: if this user does not have permission

    '''
    # Map each verify_mode to a set of verification functions
    mode_to_perm = {
        'simple': {
            'r': partial(os.access, mode=os.R_OK),
            'w': partial(os.access, mode=os.W_OK),
            'x': partial(os.access, mode=os.X_OK)
        },
        'real': {
            'r': partial(_real_r, entry_type),
            'w': partial(_real_w, entry_type),
            'x': partial(_real_x, entry_type)
        }
    }

    # Map each set of functions to individual functions
    perm_to_fun = mode_to_perm[verify_mode]

    # Track which permissions are expected to pass, using rwx for user-friendly logging
    have = {
        'r': int(user_perm) & 4 == 4,
        'w': int(user_perm) & 2 == 2,
        'x': int(user_perm) & 1 == 1
    }

    # Real file x requires r and x
    if verify_mode == 'real' and entry_type == 'file' and not have['r']:
        have['x'] = False

    # Real directory w requires w and x
    if verify_mode == 'real' and entry_type == 'dir' and not have['x']:
        have['w'] = False

    # Verify each permission
    for perm in 'rwx':
        if perm_to_fun[perm](path) != have[perm]:
            raise VerifyPermsError(
                f'Expected {perm} to {"pass" if have[perm] else "fail"} on "{path}"')


def _real_r(entry_type, path):
    '''Verify real read permission on an entry.

    Args:
        entry_type (str): file or dir
        path (str): the path to verify

    Returns:
        bool: whether the current user has permission

    '''
    if entry_type == 'file':
        try:
            with open(path, 'r', encoding='utf-8') as file:
                return file.read(1) is not None
        except PermissionError:
            return False
    if entry_type == 'dir':
        try:
            return os.listdir(path) is not None
        except PermissionError:
            return False
    return False


def _real_w(entry_type, path):
    '''Verify real write permission on an entry.

    NOTE: Directory W requires W and X

    Args:
        entry_type (str): file or dir
        path (str): the path to verify

    Returns:
        bool: whether the current user has permission

    '''
    if entry_type == 'file':
        try:
            # Always write a line that allows the file to be executable
            with open(path, "w", encoding='utf-8') as file:
                data = '#!/usr/bin/env bash\n'
                return file.write(data) == len(data)
        except PermissionError:
            return False
    if entry_type == 'dir':
        try:
            with tempfile.TemporaryFile(dir=path, mode='w') as file:
                return True
        except PermissionError:
            return False
    return False


def _real_x(entry_type, path):
    '''Verify real execute permission on an entry.

    NOTE: File X requires R and X

    Args:
        entry_type (str): file or dir
        path (str): the path to verify

    Returns:
        bool: whether the current user has permission

    '''
    if entry_type == 'file':
        return run_local(logger, path, verbose=False).passed
    if entry_type == 'dir':
        try:
            os.chdir(path)
            return True
        except PermissionError:
            return False
    return False


def _as_user(uid_gid, target, *args):
    '''Execute a method as some user.

    Args:
        uid_gid (tuple): the uid and gid to set
        target (method): method to execute
        args (obj, optional): method arguments

    Raises:
        PermissionError: on failure to set uid or gid

    '''
    uid, gid = uid_gid

    # Use a queue to propagate exceptions from the process
    error_queue = Queue()

    def _run_as_user():
        '''Set uid/gid before executing the method.'''
        try:
            try:
                os.setregid(gid, gid)       # pylint: disable=no-member
                os.setreuid(uid, uid)       # pylint: disable=no-member
            except PermissionError as error:
                raise PermissionError(f'Failed to set uid={uid}, gid={gid}') from error
            target(*args)
        except Exception as error:  # pylint: disable=broad-except
            error_queue.put((error, traceback.format_exc()))
            sys.exit(1)

    # Must fork a process in case the user is unable to switch back to the current user
    process = Process(target=_run_as_user)
    process.start()
    process.join()
    if not error_queue.empty():
        error, _traceback = error_queue.get()
        raise type(error)(_traceback)


def _parse_perms(perms):
    '''Parse rwx or octal permissions from a list.

    Args:
        perms (list): permissions to parse

    Raises:
        ValueError: on invalid input

    Returns:
        list: octal permissions in string format

    '''
    parsed_perms = []
    for perm in perms:
        if perm == 'all':
            parsed_perms.extend(_all_octal())
            continue
        if perm.isdigit():
            perm = perm.rjust(3, '0')
            if not 000 <= int(perm) <= 777:
                raise ValueError(f'Invalid perm: {perm}')
            parsed_perms.append(perm)
        else:
            parsed_perms.append(_rwx_to_oct(perm))
    return parsed_perms


def _all_octal():
    '''Get all octal permission combos.

    Returns:
        generator: all combos. I.e. ('000', '001', ...)

    '''
    perms = list(map(str, range(8)))
    return (''.join(p) for p in product(perms, perms, perms))


def _rwx_to_oct(perm):
    '''Convert an rwx string to octal permissions.

    E.g.: "rwx---r--" -> "704"

    Args:
        perm (str): the rwx string

    Returns:
        str: octal permissions

    '''
    if len(perm) > 9:
        raise ValueError(f'Invalid perm: {perm}')
    if len(perm) < 9:
        perm = perm.ljust(9, '-')
    oct_str = ''
    mask = 'rwx'
    mask_int = (4, 2, 1)
    for start_idx, end_idx in ((0, 3), (3, 6), (6, 9)):
        perm_sum = 0
        for idx, char in enumerate(perm[start_idx:end_idx]):
            if char == '-':
                continue
            if char != mask[idx]:
                raise ValueError(f'Invalid perm: {perm}')
            perm_sum += mask_int[idx]
        oct_str += str(perm_sum)
    return oct_str


def main():
    '''main execution of this program'''
    parser = ArgumentParser()
    parser.add_argument(
        'path',
        type=str,
        help='path to verify permissions of')
    parser.add_argument(
        'perms',
        nargs='+',
        type=str,
        help='octal permissions to verify. use "all" for all permissions')
    parser.add_argument(
        '-o', '--owner',
        type=str,
        help='user to run chmod as and verify "owner" permissions')
    parser.add_argument(
        '-gu', '--group-user',
        type=str,
        help='user to verify "group" permissions')
    parser.add_argument(
        '-ou', '--other-user',
        type=str,
        help='user to verify "other" permissions')
    parser.add_argument(
        '-m', '--verify-mode',
        choices=['simple', 'real'],
        default='simple',
        help='permission verification mode')
    parser.add_argument(
        '-c', '--create-type',
        choices=['file', 'dir'],
        help='create the path if non-existent')
    parser.add_argument(
        '-nc', '--no-chmod',
        action='store_true',
        help='do not chmod the entry before verifying permissions')
    args = parser.parse_args()

    # Conditionally create the file/dir
    if args.create_type:
        create(args.path, args.create_type, args.owner)

    # Run the verification
    verify(
        args.path, args.perms, args.owner, args.group_user, args.other_user, args.verify_mode,
        not args.no_chmod)

    # Delete if we created
    if args.create_type:
        delete(args.path)

    return 0


if __name__ == '__main__':
    sys.exit(main())
