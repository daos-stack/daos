from enum import Enum


class VosValueError(Exception):
    pass


class KeyType(Enum):
    HASHED = "hashed"
    INTEGER = "integer"


class Overhead(Enum):
    USER = "user"
    META = "meta"


class ValType(Enum):
    SINGLE = "single_value"
    ARRAY = "array"


class StrBool(Enum):
    YES = "Yes"
    NO = "No"


class VosBase(object):
    def __init__(self, count):
        self._payload = dict()
        self.set_count(count)

    def dump(self):
        return self._payload

    def set_count(self, count):
        if count is None:
            return
        if not isinstance(count, int):
            raise TypeError("count parameter must be of type int")
        self._payload["count"] = count

    def _get_value_from_enum(self, data):
        if isinstance(data, str):
            return data
        if data is not None:
            return data.value
        return data


class VosValue(VosBase):
    def __init__(self, size=None, count=1, aligned=None):
        super(VosValue, self).__init__(count)
        self._set_size(size)
        self._set_aligned(aligned)

    def _set_size(self, size):
        if size is None:
            raise ValueError("size parameter is required")
        if not isinstance(size, int):
            raise TypeError("size parameter must be of type int")
        self._payload["size"] = size

    def _set_aligned(self, aligned):
        aligned = self._get_value_from_enum(aligned)
        if aligned is None:
            aligned = StrBool.YES.value
        elif aligned is not StrBool.YES.value and aligned is not StrBool.NO.value:
            raise TypeError(
                "aligned parameter must be of type {0}".format(
                    type(StrBool)))

        self._payload["aligned"] = aligned


class VosItems(VosBase):
    def __init__(
            self,
            count=None,
            values=None,
            values_label=None,
            values_type=None):
        super(VosItems, self).__init__(count)
        self._values_label = values_label
        self._values_type = values_type
        self._payload[self._values_label] = list()
        self._add_values(values)

    def dump(self):
        if not bool(self._payload[self._values_label]):
            raise VosValueError(
                "list of {0} must not be empty".format(
                    self._values_label))
        return self._payload

    def add_value(self, value):
        self._check_value_type(value)
        self._payload[self._values_label].append(value.dump())

    def _add_values(self, values):
        for value in values:
            self._check_value_type(value)
            self._payload[self._values_label].append(value.dump())

    def _check_value_type(self, value):
        if not isinstance(value, self._values_type):
            raise TypeError(
                "item {0} must be of type {1}".format(
                    value, type(
                        self._values_type)))


class VosKey(VosItems):
    def __init__(
            self,
            key=None,
            count=None,
            key_type=None,
            overhead=None,
            values=None,
            values_label=None,
            values_type=None):
        super(VosKey, self).__init__(count, values, values_label, values_type)
        self._set_type(key, key_type)
        self._set_overhead(overhead)

    def _set_overhead(self, overhead):
        overhead = self._get_value_from_enum(overhead)
        if overhead is None or overhead == Overhead.USER.value:
            self._payload["overhead"] = Overhead.USER.value
        elif overhead == Overhead.META.value:
            self._payload["overhead"] = Overhead.META.value
        else:
            raise TypeError(
                "overhead parameter must be of type {0}".format(
                    type(Overhead)))

    def _add_key_size(self, key):
        if key:
            key_size = len(key.encode("utf-8"))
            self._payload["size"] = key_size
        else:
            self._payload["size"] = 0

    def _set_type(self, key, key_type):
        key_type = self._get_value_from_enum(key_type)
        if key_type is None or key_type == KeyType.HASHED.value:
            self._payload["type"] = KeyType.HASHED.value
            self._add_key_size(key)
        elif key_type == KeyType.INTEGER.value:
            self._payload["type"] = KeyType.INTEGER.value
        else:
            raise TypeError(
                "key_type parameter must be of type {0}".format(
                    type(KeyType)))


class AKey(VosKey):
    def __init__(
            self,
            key=None,
            count=1,
            key_type=None,
            overhead=None,
            value_type=None,
            values=[]):
        super(
            AKey,
            self).__init__(
            key=key,
            count=count,
            key_type=key_type,
            overhead=overhead,
            values=values,
            values_label="values",
            values_type=VosValue)
        self._set_value_type(value_type)

    def _set_value_type(self, value_type):
        value_type = self._get_value_from_enum(value_type)
        if value_type is None:
            raise ValueError("value_type parameter is required")
        elif value_type == ValType.ARRAY.value or value_type == ValType.SINGLE.value:
            self._payload["value_type"] = value_type
        else:
            raise TypeError(
                "value_type parameter must be of type {0}".format(
                    type(ValType)))


class DKey(VosKey):
    def __init__(
            self,
            key=None,
            count=1,
            key_type=None,
            overhead=None,
            akeys=[]):
        super(
            DKey,
            self).__init__(
            key,
            count,
            key_type,
            overhead,
            akeys,
            "akeys",
            AKey)


class VosObject(VosItems):
    def __init__(self, count=1, dkeys=[], targets=0):
        super(VosObject, self).__init__(count, dkeys, "dkeys", DKey)
        self.set_num_of_targets(targets)

    def set_num_of_targets(self, targets):
        if not isinstance(targets, int):
            raise TypeError("targets parameter must be of type int")
        self._payload["targets"] = targets


class Container(VosItems):
    def __init__(self, count=1, csum_size=0, csum_gran=16384, objects=[]):
        super(Container, self).__init__(count, objects, "objects", VosObject)
        self.set_csum_size(csum_size)
        self.set_csum_gran(csum_gran)

    def set_csum_size(self, csum_size):
        if not isinstance(csum_size, int):
            raise TypeError("csum_size parameter must be of type int")
        self._payload["csum_size"] = csum_size

    def set_csum_gran(self, csum_gran):
        if not isinstance(csum_gran, int):
            raise TypeError("csum_gran parameter must be of type int")
        self._payload["csum_gran"] = csum_gran


class Containers(VosItems):
    def __init__(self, num_shards=1000, containers=[]):
        super(Containers, self).__init__(
            count=None,
            values=containers,
            values_label="containers",
            values_type=Container)
        self.set_num_shards(num_shards)

    def dump(self):
        containers = super(Containers, self).dump()
        containers["num_shards"] = self._num_shards
        return containers

    def set_num_shards(self, num_shards):
        if not isinstance(num_shards, int):
            raise TypeError("num_shards parameter must be of type int")
        self._num_shards = num_shards
