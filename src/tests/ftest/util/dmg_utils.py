#!/usr/bin/python
'''
    (C) Copyright 2019 Intel Corporation.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

    GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
    The Government's rights to use, modify, reproduce, release, perform, display,
    or disclose this software are subject to the terms of the Apache License as
    provided in Contract No. B609815.
    Any reproduction of computer software, computer software documentation, or
    portions thereof marked with this legend must also reproduce the markings.
    '''
import os
import json

class DmgFailed(Exception):
    """Raise if DMG command or related activity doesn't work."""

def get_dmg_script(dmgparams, testparams, basepath):
    '''
    Build a dmg command line
    '''

    with open(os.path.join(basepath, ".build_vars.json")) as afile:
        build_paths = json.load(afile)
    orterun_bin = os.path.join(build_paths["OMPI_PREFIX"], "bin/orterun")

    script = []
    parambase = "/run/" + dmgparams + "/"
    commands = testparams.get("commands", parambase)

    # commands are a list of lists of tuples
    for command in commands:
        dmg_cmd = os.path.join(build_paths["OMPI_PREFIX"], "bin/dmg")
        for token in command:
            dmg_cmd += " " + token[1]
        complete_cmd = orterun_bin + " -N 1 --host $SLURM_NODELIST " \
                        + dmg_cmd + "\n"
        script.append(complete_cmd)

    return script
