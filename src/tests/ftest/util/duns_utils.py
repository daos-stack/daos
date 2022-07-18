#!/usr/bin/env python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""


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
        for attr_name in ('identifier', 'label', 'uuid'):
            try:
                val = getattr(obj, attr_name)
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
