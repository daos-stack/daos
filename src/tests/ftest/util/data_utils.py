"""
  (C) Copyright 2023 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import statistics


def list_unique(data):
    """Get the unique elements in a list.

    Args:
        data (list): list to get unique elements from

    Returns:
        list: the unique elements

    """
    unique = []
    for item in data:
        if item not in unique:
            unique.append(item)
    return unique


def list_flatten(data):
    """Flatten a 2-dimensional list into 1 dimension.

    Only flattens elements that are lists. For example:
    [[1, 2], [3, 4], 5] -> [1, 2, 3, 4, 5]

    Args:
        data (list): list to flatten

    Returns:
        list: the flattened list

    """
    flat = []
    for sub_list in data:
        if isinstance(sub_list, (list, tuple)):
            flat.extend(sub_list)
        else:
            flat.append(sub_list)
    return flat


def list_stats(values):
    """Get a dictionary of statistics for a list of values.

    Args:
        values (list): list of numerical values

    Returns:
        dict: dictionary containing mean, min, max

    """
    return {
        'mean': statistics.mean(values),
        'min': min(values),
        'max': max(values)
    }


def dict_extract_values(obj, key_path, val_type=object):
    """Extract all values of a key structure from a nested dictionary.

    For example:
        obj = {
            'a': {'x': 1, 'y': 2},
            'b': {'x': 3, 'y': 4}}
        key_path = ['x']
            -> [1, 3]
        key_path = ['a', 'x']
            -> [1]

    Args:
        obj (dict/list): dictionary or list to extract from
        key_path (list): dictionary key or list index structure to extract values of.
            Use '*' for wildcard key
        val_type (object, optional): type or tuple of types to filter values by.
            Defaults to object, which does not filter by type

    Raises:
        TypeError: if obj is not a dict, list, or tuple
        RecursionError: if max recursion depth is exceeded

    Returns:
        list: list of values extracted

    """
    if not isinstance(obj, (dict, list, tuple)):
        raise TypeError('obj must be dict, list, or tuple')

    def _extract(obj, key_path, first_key, max_depth):
        if not key_path:
            return obj
        if max_depth <= 0:
            raise RecursionError('maximum recursion depth exceeded')
        extracted = []
        key = key_path[0]
        next_keys = key_path[1:]
        if isinstance(obj, dict):
            items = obj.items()
        elif isinstance(obj, (list, tuple)):
            items = enumerate(obj)
        else:
            return []
        for _key, _val in items:
            if key in (_key, '*'):
                if not next_keys:
                    # Last key found - this is the value we want
                    if isinstance(_val, val_type):
                        extracted.extend([_val])
                else:
                    # Need to recurse to next key
                    extracted.extend(_extract(_val, next_keys, False, max_depth - 1))

            if first_key:
                # Keep looking for the first key, even if we found it above.
                # This supports, e.g., {'a': {'b': 'a': 0}}
                extracted.extend(_extract(_val, key_path, True, max_depth - 1))
        return extracted
    return _extract(obj, key_path, first_key=True, max_depth=20)


def dict_subtract(dict1, dict2):
    """Dictionary dict1 - dict2 for numerical leaf values.

    Assumes both dictionaries have the same structure.

    Args:
        dict1 (dict): left-side dictionary
        dict2 (dict): right-side dictionary

    Raises:
        TypeError: if leaf values are not numbers

    Returns:
        dict: dict1 - dict2 with leaf values subtracted

    """
    dict3 = {}
    for key, val in dict1.items():
        if isinstance(val, dict):
            dict3[key] = dict_subtract(val, dict2[key])
        else:
            try:
                dict3[key] = val - dict2[key]
            except TypeError as error:
                raise TypeError('Invalid type for key {}'.format(key)) from error
    return dict3


def assert_val_in_list(val, allowed_list):
    """Assert whether a value is in the allowed list.

    Args:
        val (object): value to check
        allowed_list (list): list of allowed values

    Returns:
        bool: True if val is in allowed_list

    Raises:
        AssertionError: if val is not in allowed_list
    """
    if val not in allowed_list:
        raise AssertionError(f'Expected one of {allowed_list}')
    return True


def assert_dict_subset(subset, full):
    """Assert that a dictionary is a subset of another dictionary.

    For example:
        assert_dict_subset({'a': 1}, {'a': 1, 'b': 2}) -> passes
        assert_dict_subset({'a': 2}, {'a': 1, 'b': 2}) -> AssertionError
        assert_dict_subset({'c': 2}, {'a': 1, 'b': 2}) -> AssertionError

    Args:
        subset (dict): Expected subset dictionary, where only keys in the subset are verified
            in the full dictionary.
            Expected values can be type callable(actual_value) -> bool for custom verification.
        full (dict): Full dictionary to verify against the subset

    Raises:
        AssertionError: if subset is not contained in full
    """

    def _format_keys(keys):
        """Convert list of keys to ["key1"]["key2"] format."""
        return ''.join(map(lambda k: f'["{k}"]', keys))

    def _assert_subset(prev_keys, expected, actual):
        """Recursively verify expected dict matches actual dict."""
        for key, expected_value in expected.items():
            cur_keys = prev_keys + [key]
            try:
                actual_value = actual[key]
            except KeyError as error:
                raise AssertionError(
                    f'Missing expected key {_format_keys(cur_keys)}') from error

            if callable(expected_value):
                # Use custom callable to verify value
                try:
                    if not expected_value(actual_value):
                        raise AssertionError(
                            f'{_format_keys(cur_keys)} = {actual_value} ; '
                            f'expected to satisfy {expected_value.__name__}')
                except AssertionError as error:
                    # If the custom callable raised an AssertionError, use its error message
                    raise AssertionError(
                        f'{_format_keys(cur_keys)} = {actual_value} ; {str(error)}') from error

            elif type(expected_value) is not type(actual_value):
                # Types must match
                raise AssertionError(
                    f'type({_format_keys(cur_keys)}) = {type(actual_value)} ; '
                    f'expected {type(expected_value)}')

            elif isinstance(expected_value, dict):
                # Recursively verify nested dict
                _assert_subset(cur_keys, expected_value, actual_value)

            elif expected_value != actual_value:
                # Compare leaf values
                raise AssertionError(
                    f'{_format_keys(cur_keys)} = {actual_value} ; expected "{expected_value}"')

    _assert_subset([], subset, full)
