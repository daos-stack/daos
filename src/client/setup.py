#
# setup.py for packaging pydaos python module.
#
# To use type:
#
# python3 setup.py install
#
# If run from within a compiled DAOS source tree this it will detect the
# install path automatically, otherwise it'll use the defaults.

import os
import sys
import json


def load_conf():
    """Load the build config file"""
    file_self = os.path.dirname(os.path.abspath(__file__))
    json_file = None
    while file_self != '/':
        new_file = os.path.join(file_self, '.build_vars.json')
        if os.path.exists(new_file):
            json_file = new_file
            ofh = open(new_file, 'r')
            conf = json.load(ofh)
            return conf

        file_self = os.path.dirname(file_self)
    return None


from setuptools import setup, find_packages, Extension

conf = load_conf()

args = {'sources': ['pydaos/pydaos_shim.c'],
        'libraries': ['daos', 'duns']}

if conf:
    args['include_dirs'] = [os.path.join(conf['PREFIX'], 'include')]
    if conf.get('CART_PREFIX', None):
        args['include_dirs'].extend(os.path.join(
            conf['CART_PREFIX'], 'include'))
    args['library_dirs'] = [os.path.join(conf['PREFIX'], 'lib64')]
    args['runtime_library_dirs'] = args['library_dirs']


args['define_macros'] = [('__USE_PYTHON3__', 1)]

module1 = Extension('pydaos.{}'.format("pydaos_shim"), **args)

setup(
    name='pydaos',
    version='0.2',
    packages=find_packages(),
    description='DAOS interface',
    ext_modules=[module1]
)
