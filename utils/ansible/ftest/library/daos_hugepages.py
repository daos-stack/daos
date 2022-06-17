#!/usr/bin/env python
# -*- coding: utf-8 -*-
# pylint: disable=no-name-in-module
# pylint: disable=import-error
# pylint: disable=broad-except
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""


__copyright__ = """
(C) Copyright 2018-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""


ANSIBLE_METADATA = {
    'metadata_version': '0.1',
    'status': ['preview'],
    'supported_by': 'Intel'
}


DOCUMENTATION = '''
---
module: daos_hugepages

short_description: Configure Linux Huge Pages

version_added: "0.1"

description:
    - Check if the Linux Huge Pages feature is enabled
    - Check the value of the Huge Pages
    - Set the value of the Huge Pages if needed

options:
    size:
        description:
            - Number of huge pages to allocate
        type: int
        default: 4096

author:
    - DAOS group <daos@daos.groups.io>
'''

EXAMPLES = '''
- name: Set the Linux Huge Pages size
  daos_hugepages
'''

RETURN = '''
elapsed:
     description: the number of seconds that elapsed while checking and setting the huge pages size
     returned: always
     type: int
     sample: 3
'''

import os
import re
import datetime
import subprocess # nosec B404

from ansible.module_utils.basic import AnsibleModule

__HUGEPAGES_STATE_PATTERN__ = re.compile(r"(\[madvise\])|(\[always\])")
__HUGEPAGES_SYSCTL_PATTERN = re.compile(r"^vm\.nr_hugepages\s*=\s*(?P<size>\d+)$")

def is_huge_pages_enabled():
    """Check if huge pages are enabled

    Returns:
        bool: true if huge pages are enabled
    """
    with open(r'/sys/kernel/mm/transparent_hugepage/enabled', 'r', encoding="utf8") as fd:
        line = fd.read()
        return __HUGEPAGES_STATE_PATTERN__.match(line)

def main():
    """Ansible module setting the Huge Pages size."""

    ### Default Ansible module prologue ###
    argument_spec = dict(size=dict(type='int', default=4096))
    module = AnsibleModule(argument_spec=argument_spec, supports_check_mode=True)
    check_mode = module.check_mode
    hugepages_size = module.params['size']

    ### Huge Pages configuration
    start_time = datetime.datetime.utcnow()
    if not os.path.isfile(r"/sys/kernel/mm/transparent_hugepage/enabled"):
        module.fail_json(elapsed=(datetime.datetime.utcnow() - start_time).seconds,
                msg="Huge Pages not activated in kernel")

    if not is_huge_pages_enabled():
        try:
            with open(r"/sys/kernel/mm/transparent_hugepage/enabled", 'w', encoding="utf8") as fd:
                fd.write('madvise')
        except Exception as ex:
            module.fail_json(elapsed=(datetime.datetime.utcnow() - start_time).seconds,
                    msg=f"Huge Pages could not be enabled: {ex}")
        if not is_huge_pages_enabled():
            module.fail_json(elapsed=(datetime.datetime.utcnow() - start_time).seconds,
                    msg="Huge Pages could not be enabled")

    cp = subprocess.run([r'sysctl', r'vm.nr_hugepages'],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=3, check=False)
    if cp.returncode != 0:
        module.fail_json(elapsed=(datetime.datetime.utcnow() - start_time).seconds,
                msg=f"Size of Huge Pages could not be read: {cp.stderr.decode('ascii')}")

    hugepages_current_size = 0
    stdout_str = cp.stdout.decode('ascii')
    m = __HUGEPAGES_SYSCTL_PATTERN.match(stdout_str)
    if m is None:
        module.fail_json(elapsed=(datetime.datetime.utcnow() - start_time).seconds,
                msg=f"Invalid size of huge pages from sysctl: {stdout_str}")
    hugepages_current_size = int(m.groupdict()['size'])

    if hugepages_size != hugepages_current_size:
        if check_mode:
            module.fail_json(elapsed=(datetime.datetime.utcnow() - start_time).seconds,
                    msg=f"Invalid size of huge pages: {hugepages_current_size}")

        cp = subprocess.run([r'sysctl', f"vm.nr_hugepages={hugepages_size}"],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=3, check=False)
        if cp.returncode != 0:
            module.fail_json(elapsed=(datetime.datetime.utcnow() - start_time).seconds,
                    msg="Size of Huge Pages could not be dynamically set: "
                    f"{cp.stderr.decode('ascii')}")

        try:
            with open(r"/etc/sysctl.d/50-hugepages.conf", "w", encoding="utf8") as fd:
                fd.write(f"vm.nr_hugepages={hugepages_size}")
        except Exception as ex:
            module.fail_json(elapsed=(datetime.datetime.utcnow() - start_time).seconds,
                    msg=f"Setup of Huge Pages size at boot could not be defined: {ex}")
        cp = subprocess.run([r'sysctl', '-p'], stderr=subprocess.PIPE, timeout=3, check=False)
        if cp.returncode != 0:
            module.fail_json(elapsed=(datetime.datetime.utcnow() - start_time).seconds,
                    msg="Setup of Huge Pages size at boot could not be applied: "
                    f"{cp.stderr.decode('ascii')}")

        module.exit_json(changed=True,
                         elapsed=(datetime.datetime.utcnow() - start_time).seconds)

    module.exit_json(changed=False,
                     elapsed=(datetime.datetime.utcnow() - start_time).seconds)


if __name__ == '__main__':
    main()
