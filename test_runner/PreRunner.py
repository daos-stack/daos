#!/usr/bin/env python3
# Copyright (c) 2016 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
# -*- coding: utf-8 -*-
"""
test runner class

"""

import os
import tempfile


class PreRunner():
    """setup for test runner"""
    test_info = {}
    test_directives = {}
    info = None

    def set_key_from_host(self):
        """ add to default environment """
        module = self.test_info['module']
        host_list = self.info.get_config('host_list')
        hostkey_list = module.get('setKeyFromHost')
        host_config = module.get('hostConfig')
        if not host_config or host_config['type'] == 'oneToOne':
            for k in range(0, len(hostkey_list)):
                self.test_info['defaultENV'][hostkey_list[k]] = host_list[k]
        elif host_config['type'] == 'buildList':
            items = ","
            end = host_config['numServers']
            server_list = items.join(host_list[0:end])
            self.test_info['defaultENV'][hostkey_list[0]] = server_list
            start = host_config['numServers']
            end = start + host_config['numClients']
            client_list = items.join(host_list[start:end])
            self.test_info['defaultENV'][hostkey_list[1]] = client_list

    def set_key_from_info(self):
        """ add to default environment """
        module = self.test_info['module']
        key_list = module.get('setKeyFromInfo')
        for item in range(0, len(key_list)):
            (k, v, ex) = key_list[item]
            self.test_info['defaultENV'][k] = self.info.get_info(v) + ex

    def create_append_key_from_info(self, append=False):
        """ add to default environment """
        save_value = ""
        module = self.test_info['module']
        if append:
            key_list = module.get('appendKeyFromInfo')
        else:
            key_list = module.get('createKeyFromInfo')
        for var in range(0, len(key_list)):
            (k, ex, vlist) = key_list[var]
            if append:
                save_value = self.test_info['defaultENV'].get(k, (os.getenv(k)))
            new_list = []
            for var_name in vlist:
                var_value = self.info.get_info(var_name) + ex
                if not save_value or var_value not in save_value:
                    new_list.append(var_value)
            items = ":"
            new_value = items.join(new_list)
            if not new_value:
                self.test_info['defaultENV'][k] = save_value
            elif save_value:
                self.test_info['defaultENV'][k] = \
                    new_value + ":" + save_value
            else:
                self.test_info['defaultENV'][k] = new_value

    def set_key_from_config(self):
        """ add to default environment """
        config_key_list = self.info.get_config('setKeyFromConfig')
        for (key, value) in config_key_list.items():
            self.test_info['defaultENV'][key] = value

    def set_directive_from_config(self):
        """ add to default test directives """
        config_key_list = self.info.get_config('setDirectiveFromConfig')
        for (key, value) in config_key_list.items():
            self.test_directives[key] = value

    def create_tmp_dir(self):
        """ add to default environment """
        envName = self.test_info['module']['createTmpDir']
        tmpdir = tempfile.mkdtemp()
        self.test_info['defaultENV'][envName] = tmpdir

    def add_default_env(self):
        """ add to default environment """
        module = self.test_info['module']
        if self.info.get_config('host_list') and module.get('setKeyFromHost'):
            self.set_key_from_host()
        if module.get('setKeyFromInfo'):
            self.set_key_from_info()
        if module.get('createKeyFromInfo'):
            self.create_append_key_from_info()
        if module.get('appendKeyFromInfo'):
            self.create_append_key_from_info(True)
        if self.info.get_config('setKeyFromConfig'):
            self.set_key_from_config()
        if self.info.get_config('setDirectiveFromConfig'):
            self.set_directive_from_config()
        if module.get('createTmpDir'):
            self.create_tmp_dir()

    def setup_default_env(self):
        """ setup default environment """
        module_env = self.test_info['defaultENV']
        for (key, value) in module_env.items():
            os.environ[str(key)] = value
