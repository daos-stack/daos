"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import re


def format_path(pool=None, cont=None, path=None):
    """Format a DAOS UNS path of the form daos://pool/cont/path

    Args:
        pool (str/obj, optional): pool string or object containing identifier, label, or uuid.
        cont (str/obj, optional): cont string or object containing identifier, label, or uuid.
            Default is None.
        path (str, optional): path relative to container root.
            Default is None.

    Returns:
        str: the DAOS UNS path
    """
    def _get_id(obj):
        try:
            val = getattr(obj, 'identifier')
            if val:
                return val
        except AttributeError:
            pass
        try:
            val = getattr(obj, 'label').value
            if val:
                return val
        except AttributeError:
            pass
        try:
            val = getattr(obj, 'uuid')
            if val:
                return val
        except AttributeError:
            pass
        return obj

    uns_path_list = ['daos:/']

    if pool is not None:
        uns_path_list.append(_get_id(pool))
    if cont is not None:
        uns_path_list.append(_get_id(cont))
    if path is not None:
        uns_path_list.append(path.lstrip('/'))

    return '/'.join(uns_path_list)


def parse_path(path):
    """Parse a DAOS UNS path of the form daos://pool/cont/path

    Args:
        path (str): DAOS UNS path to parse

    Returns:
        (str, str, str): pool, container, path
    """
    _pool = None
    _cont = None
    _path = None
    match = re.search(r'daos:/[/]+([^/]+)(/*[^/]*/?)(.*)', path)
    try:
        _pool = match.group(1) or None
        _cont = match.group(2).strip('/') or None
        _path = match.group(3).lstrip('/') or None
        if _path:
            _path = '/' + _path
    except (AttributeError, IndexError):
        pass
    return _pool, _cont, _path
