"""Creates GCP-specific functional test (ftest) yaml files.

The OSS DAOS ftests can't be run on GCP with the existing yaml files. This
module generates GCP-specific ftest yaml files that can run successfully.
"""
# !/usr/bin/python3

import copy
import glob
import math
import os
# Replace typing.Union with '|' once the docker image is upgraded to Python 3.10
from typing import NamedTuple, Union

import parse
import ruamel.yaml

# The number of targets per server.
_SERVER_TARGETS = 16

# By default, the tier ratio is 6:94.
# Given a 100 TB pool size, there will be 6 TB of SCM and 94 TB of NVMe.
_POOL_SIZE_SCM_TIER_RATIO = 0.06
_POOL_SIZE_NVME_TIER_RATIO = 0.94

_BYTE_CONVERSIONS = {
    "K": pow(10, 3), "KB": pow(10, 3), "KiB": pow(2, 10),
    "M": pow(10, 6), "MB": pow(10, 6), "MiB": pow(2, 20),
    "G": pow(10, 9), "GB": pow(10, 9), "GiB": pow(2, 30),
    "T": pow(10, 12), "TB": pow(10, 12), "TiB": pow(2, 40),
}


class _SizeField(NamedTuple):
    value: float
    unit: str = ""


def _multiply_test_servers(
        servers: Union[int, str], multiplier: int
) -> Union[int, str]:
    if isinstance(servers, int):
        return servers * multiplier

    parsed = parse.parse("server-[{:d}-{:d}]", servers)
    number_of_servers = (parsed[1] - parsed[0] + 1) * multiplier

    return "server-[%d-%d]" % (parsed[0], parsed[0] + number_of_servers - 1)


def _convert_multibyte_unit_to_bytes(unit_str: str) -> Union[int, float]:
    if not unit_str:
        # A byte has a unit value of 1
        return 1
    return _BYTE_CONVERSIONS[unit_str]


# Replace with str.removesuffix once the docker image is upgraded to Python 3.9
def _remove_suffix(input_string: str, suffix: str) -> str:
    if suffix and input_string.endswith(suffix):
        return input_string[: -len(suffix)]
    return input_string


def _get_size_field(
        size: Union[int, str], convert_to_bytes: bool = True
) -> _SizeField:
    """Parse the size field from the yaml file.

    Args:
        size: The size value from the yaml file.
        convert_to_bytes: Whether to convert the size value to bytes.

    Returns:
        A named tuple containing the size value and unit.
    Raises:
        ValueError: If the size input has an invalid unit.
    """
    if isinstance(size, int):
        return _SizeField(value=size)

    for unit_str in _BYTE_CONVERSIONS:
        if size.endswith(unit_str):
            number = float(_remove_suffix(size, unit_str))
            if convert_to_bytes:
                return _SizeField(
                    value=number * _convert_multibyte_unit_to_bytes(unit_str)
                )

            return _SizeField(value=number, unit=unit_str)

    raise ValueError(f"Unable to parse size input: {size}")


class ModifyYAML:
    """Modifies the ftest yaml file to make it GCP-compatible.

    Attributes:
        yaml_original: The original yaml file.
        yaml: The modified yaml file.
    """

    def __init__(self, yaml_original: ruamel.yaml.YAML):
        self.yaml_original = yaml_original
        self.yaml_original.preserve_quotes = True
        self.yaml: ruamel.yaml.YAML = None

    def update(self) -> bool:
        """Processes the yaml file to make it GCP-compatible.

        Returns:
            True if the modified yaml is different from the original, else false.
        """
        self.yaml = copy.deepcopy(self.yaml_original)

        # Shouldn't be needed since we are no longer running ior ftests
        # self._adjust_ppn_for_ior()
        # self._adjust_objectclass_for_ior()
        self._adjust_challenger_path()
        self._adjust_server_config()
        self._fix_oclasses()
        self._add_or_update_daos_server_timeout(min_timeout=180)
        self._adjust_test_timeouts(multiplier=8)

        return self.yaml != self.yaml_original

    def _adjust_ppn_for_ior(self) -> None:
        """Adjust to whether we use ppn or --map-by syntax depending on MPI used.

        For OpenMPI use '-ppn {}' to set the number of processes per node.
        For MPICH, use '--map-by ppr:{}:node'.
        """
        uses_mpich = (
            "mdtest" in self.yaml
            and "manager" in self.yaml["mdtest"]
            and self.yaml["mdtest"]["manager"] == "MPICH"
        )
        uses_mpich |= (
            "hdf5_vol" in self.yaml
            and "plugin_path" in self.yaml["hdf5_vol"]
            and self.yaml["hdf5_vol"]["plugin_path"] == "/usr/lib64/mpich/lib"
        )
        uses_mpich |= (
            "mpi_module" in self.yaml and "mpich" in self.yaml["mpi_module"]
        )
        uses_mpich |= "test_repo" in self.yaml and "hdf5" in self.yaml["test_repo"]

        if uses_mpich:
            return

        ppn = self.yaml["ior"]["client_processes"]["ppn"]
        del self.yaml["ior"]["client_processes"]["ppn"]
        # FTest framework converts this to '--map-by ppr:{}:node'
        self.yaml["ior"]["client_processes"]["pprnode"] = ppn

    def _adjust_objectclass_for_ior(self) -> None:
        if (
            "ior" in self.yaml
            and "objectclass" in self.yaml["ior"]
            and len(self.yaml["ior"]["objectclass"]) > 1
        ):
            classes = []
            for label in list(self.yaml["ior"]["objectclass"]):
                classes.append(self.yaml["ior"]["objectclass"][label]["dfs_oclass"])
                del self.yaml["ior"]["objectclass"][label]
            self.yaml["ior"]["objectclass"]["dfs_oclass"] = classes

    def _adjust_challenger_path(self) -> None:
        """Prevents error: FindCmd Test Failed: Error running 'mkdir -p /mnt/lustre/..'."""
        if "perf" in self.yaml and "challenger_path" in self.yaml["perf"]:
            self.yaml["perf"]["challenger_path"] = "/root/tmp/mnt/lustre"

    def _adjust_server_config(self) -> None:
        if "server_config" in self.yaml:
            self._adjust_for_having_a_single_engine()
            self._remove_all_but_first_engine_config()

    def _adjust_for_having_a_single_engine(self) -> None:
        """Adjusts yaml file to account for having a single engine per node.

        rd_lvl:1 by default (rank level fault domain).
        """
        multiplier = 1
        if (
                "engines_per_host" in self.yaml["server_config"]
                and self.yaml["server_config"]["engines_per_host"] > 1
        ):
            multiplier = self.yaml["server_config"]["engines_per_host"]
            self._adjust_engines_per_host()
            self._adjust_test_servers(multiplier)
            self._adjust_test_ranks(multiplier)

        self._adjust_pool_size("pool", multiplier)
        self._adjust_pool_size("pool_acl", multiplier)

    def _adjust_engines_per_host(self) -> None:
        self.yaml["server_config"]["engines_per_host"] = 1

    def _adjust_test_servers(self, multiplier: int) -> None:
        """Adjust test_servers since we only have 1 engine per node."""
        if "test_servers" in self.yaml["hosts"]:
            self.yaml["hosts"]["test_servers"] = _multiply_test_servers(
                self.yaml["hosts"]["test_servers"], multiplier
            )
            return

        for tag in self.yaml["hosts"]:
            if isinstance(self.yaml["hosts"][tag], int):
                continue

            if "test_servers" in self.yaml["hosts"][tag]:
                self.yaml["hosts"][tag]["test_servers"] = _multiply_test_servers(
                    self.yaml["hosts"][tag]["test_servers"], multiplier
                )
                continue

            for tag2 in self.yaml["hosts"][tag]:
                if not (
                    isinstance(self.yaml["hosts"][tag][tag2], int)
                    and "test_servers" in self.yaml["hosts"][tag][tag2]
                ):

                    self.yaml["hosts"][tag][tag2]["test_servers"] = (
                        _multiply_test_servers(
                            self.yaml["hosts"][tag][tag2]["test_servers"], multiplier
                        )
                    )

    def _adjust_test_ranks(self, multiplier: int) -> None:
        if "test_ranks" in self.yaml:
            for key in self.yaml["test_ranks"]:
                if "," in self.yaml["test_ranks"][key][0]:
                    self.yaml["test_ranks"][key] = self.yaml["test_ranks"][key][0].split(
                        ","
                    )
                first_rank = int(self.yaml["test_ranks"][key][0]) // multiplier
                for i in range(len(self.yaml["test_ranks"][key])):
                    self.yaml["test_ranks"][key][i] = str(first_rank + i)
                self.yaml["test_ranks"][key] = ruamel.yaml.comments.CommentedSeq((1, 2))

    def _adjust_pool_size(
            self,
            pool: str,
            multiplier: int,
            min_scm_size_per_target_bytes: int = 16_777_216,
            max_scm_size_bytes: int = 128_000_000_000,
            min_nvme_size_per_target_bytes: int = 1_073_741_824
    ) -> None:
        """Scale pool size to account for engine multiplier.

        Scales pool size based on the fact we are using multiplier times as
        many servers and thus mave multipliers times more pool space.

        Args:
            pool: the pool to adjust.
            multiplier: the number of engines per node used in the yaml file.
            min_scm_size_per_target_bytes: the minimum size of the SCM pool, each
                target requires 16 MiB of SCM.
            max_scm_size_bytes: the maximum size of the SCM pool.
            min_nvme_size_per_target_bytes: the minimum size of the NVMe pool, each
                target requires 1 GiB of NVMe.
        """
        if pool not in self.yaml:
            return

        for key in [
                "size",
                "scm_size",
                "nvme_size"
        ]:
            if key not in self.yaml[pool]:
                continue
            # Do not need to adjust pool size if it's given as a percent
            if (
                    isinstance(self.yaml[pool][key], str)
                    and "%" in self.yaml[pool][key]
            ):
                continue

            original_size = _get_size_field(
                self.yaml[pool][key], convert_to_bytes=False
            )
            updated_size = _SizeField(
                value=original_size.value * multiplier, unit=original_size.unit
            )

            update_size_bytes = updated_size.value * _convert_multibyte_unit_to_bytes(
                updated_size.unit
            )
            if key == "size":
                servers = 1
                if "test_servers" in self.yaml["hosts"]:
                    servers = self.yaml["hosts"]["test_servers"]

                min_size_bytes = (
                    math.ceil(
                        max(
                            min_scm_size_per_target_bytes / _POOL_SIZE_SCM_TIER_RATIO,
                            min_nvme_size_per_target_bytes / _POOL_SIZE_NVME_TIER_RATIO,
                        )
                    )
                    * _SERVER_TARGETS
                    * servers
                )
                if update_size_bytes < min_size_bytes:
                    updated_size = _SizeField(value=min_size_bytes)
            elif key == "scm_size":
                min_scm_size_bytes = min_scm_size_per_target_bytes * _SERVER_TARGETS
                if update_size_bytes < min_scm_size_bytes:
                    # Assuming 16 targets, each target requires 16 MiB of SCM.
                    updated_size = _SizeField(value=min_scm_size_bytes)
                elif update_size_bytes > max_scm_size_bytes:
                    # Do not adjust scm_size if it exceeds the VM resources.
                    continue
            elif key == "nvme_size":
                min_nvme_size_bytes = min_nvme_size_per_target_bytes * _SERVER_TARGETS
                if update_size_bytes < min_nvme_size_bytes:
                    # Assuming 16 targets, each target requires 1 GiB of NVMe.
                    updated_size = _SizeField(value=min_nvme_size_bytes)

            if updated_size.unit:
                self.yaml[pool][key] = f"{updated_size.value:g}{updated_size.unit}"
            else:
                self.yaml[pool][key] = updated_size.value

    def _remove_all_but_first_engine_config(self) -> None:
        """Keeps only the first engine's config info.

        Modifies it for our use case.
        """
        if "engines" in self.yaml["server_config"]:
            first_engine = self.yaml["server_config"]["engines"][0]
            # Remove all but the first engine
            self.yaml["server_config"]["engines"] = [first_engine]

            # Delete scm_list if it exists
            if "storage" in first_engine:
                if isinstance(first_engine["storage"], dict):
                    first_engine["storage"][0].pop("scm_list", None)

            # Delete pinned numa node if it exists
            first_engine.pop("pinned_numa_node", None)

    # TODO: b/322223875 - should consider just setting this to EC_2P1GX
    def _fix_oclasses(self) -> None:
        if (
                "objectclass" in self.yaml
                and "dfs_oclass_list" in self.yaml["objectclass"]
        ):
            oclass_lists = self.yaml["objectclass"]["dfs_oclass_list"]

            for i in range(len(oclass_lists)):
                if len(oclass_lists[i]) > 1:
                    oclass_lists[i] = oclass_lists[i][0]

    def _add_or_update_daos_server_timeout(self, min_timeout: int = 40) -> None:
        """Adjusts daos_server timeout to be at least the min_timeout.

        The default timeout linearly scales with the bdev_list, the number of (nvme)
        PCI addresses. Since GCE uses a single virtual address for the 16 SSDs, a
        longer timeout waiting for the server to start after formatting storage is
        required.

        Args:
            min_timeout: The minimum timeout in seconds to use. The default value is
                from
                https://source.corp.google.com/h/cloud-daos-internal/daos/+/main-2.4:src/tests/ftest/util/server_utils_base.py;drc=5c7e312ed99a4f80b6bba39331d0da93bb37899f;l=158
        """

        if "daos_server" not in self.yaml:
            self.yaml["daos_server"] = {
                "pattern_timeout": min_timeout,
            }
            return

        if "pattern_timeout" not in self.yaml["daos_server"]:
            self.yaml["daos_server"]["pattern_timeout"] = min_timeout
            return

        self.yaml["daos_server"]["pattern_timeout"] = max(
            min_timeout, self.yaml["daos_server"]["pattern_timeout"]
        )

    def _adjust_test_timeouts(self, multiplier: int = 1) -> None:
        """Adjusts timeouts by the multiplier.

        Args:
            multiplier: The multiplier to adjust the timeouts by.

        Raises:
            ValueError: If the multiplier is less than or equal to 0.
        """
        if multiplier <= 0:
            raise ValueError("Multiplier must be greater than 0.")

        if "timeouts" not in self.yaml:
            return

        for key in self.yaml["timeouts"]:
            if isinstance(self.yaml["timeouts"][key], int):
                self.yaml["timeouts"][key] = multiplier * self.yaml["timeouts"][key]


def adjust_ftest_yaml_files(path_name: str) -> None:
    """Modifies all ftest config yaml files to run on GCP."""
    yaml = ruamel.yaml.YAML()
    yaml.preserve_quotes = True

    for filename in glob.glob(path_name):
        # Ignore files previously output by this script
        if filename.endswith(".gcp.yaml"):
            continue

        print(f"Processing file: {filename}")
        with open(filename, "r") as yaml_str:
            modify_yamls = ModifyYAML(yaml.load(yaml_str))

        if modify_yamls.update():
            # Rename the file to have a ".gcp.yaml" suffix
            new_filename = _remove_suffix(filename, ".yaml") + ".gcp.yaml"
            with open(new_filename, "w") as gcp_yaml_file:
                yaml.dump(modify_yamls.yaml, gcp_yaml_file)


def main():
    if os.geteuid() != 0:
        raise OSError("This script must be called as root.")

    adjust_ftest_yaml_files("/usr/lib/daos/TESTING/ftest/*/*.yaml")


if __name__ == "__main__":
    # Not an absl app. This file runs outside of google3, within a GCE VM.
    main()
