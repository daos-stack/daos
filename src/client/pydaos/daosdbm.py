#!/usr/bin/python3

"""Test code for named KVs within a container"""

import os
import sys
import json
import pickle

def load_conf():
    """Load the build config file"""

    file_self = os.path.dirname(os.path.abspath(__file__))
    json_file = None
    while True:
        new_file = os.path.join(file_self, '.build_vars.json')
        if os.path.exists(new_file):
            json_file = new_file
            break
        file_self = os.path.dirname(file_self)
        if file_self == '/':
            raise Exception('build file not found')
    ofh = open(json_file, 'r')
    conf = json.load(ofh)
    ofh.close()
    return conf

class daos_named_kv():
    """Named KV generator"""

    def __init__(self, puid, cuid):
        conf = load_conf()
        if sys.version_info.major < 3:
            pydir = 'python{}.{}'.format(
                sys.version_info.major, sys.version_info.minor)
        else:
            pydir = 'python{}'.format(sys.version_info.major)
        sys.path.append(os.path.join(conf['PREFIX'],
                                     'lib64',
                                     pydir,
                                     'site-packages'))

        self.daos = __import__('pydaos')

        self.container = self.daos.Cont(puid, cuid)
        self.root_kv = self.container.rootkv()

    def get_kv_by_name(self, name):
        """Return KV by name, create it if it doesn't exist"""

        if name in self.root_kv:
            return self.container.kv(pickle.loads(self.root_kv[name]))

        new_kv = self.container.newkv()
        self.root_kv[name] = pickle.dumps(new_kv.oid)
        return new_kv

    def get_kv_list(self):
        """Return a list of KVs"""

        for kv in self.root_kv:
            yield kv

LENGTH_KEY = '__length'

def new_migrate(root):
    """Migrate data from multiple KVs to a single KV"""

    batch_size = 200

    files = sorted(root.get_kv_list())

    if 'root' in files:
        files.remove('root')

    target = root.get_kv_by_name('root')

    target_offset = 0

    for ifile in files:

        index = 0

        source = root.get_kv_by_name(ifile)

        at_end = False

        while not at_end:
            data = {}
            for i in range(index, index + batch_size):

                index += 1

                data[str(i)] = None

            source.bget(data)
            tdata = {}

            for key in data:
                if data[key] is None:
                    at_end = True
                else:
                    i = int(key)
                    tdata[str(i+target_offset)] = data[key]

            target.bput(tdata)

        target_offset += pickle.loads(source[LENGTH_KEY])

        target[LENGTH_KEY] = pickle.dumps(target_offset)

def main():
    """Migrate data from dbm file to named DAOS KV"""

    if len(sys.argv) < 3:
        print('Need pool/container UUIDs')
        return
    PUID = sys.argv[1]
    CUID = sys.argv[2]

    my_kv = daos_named_kv(PUID,
                          CUID)

    print('Kvs are {}'.format(','.join(sorted(my_kv.get_kv_list()))))

    new_migrate(my_kv)
    return

if __name__ == '__main__':
    main()
