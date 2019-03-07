#!/usr/bin/python

import json
import os
import requests

def main():
    ''' main '''
    base_url = 'https://build.hpdd.intel.com/blue/rest/organizations/' + \
               'jenkins/pipelines/daos-stack/pipelines/daos/branches/' + \
               '{}/runs/{}/nodes/'.format(os.environ['BRANCH_NAME'],
                                          os.environ['BUILD_ID'])

    req = requests.get(base_url)
    nodes = json.loads(req.text)
    print "Nodes:\n", json.dumps(nodes, indent=4)
    node = None
    for node in nodes:
        print node['id'], node['displayName'], node['state']
        if node['state'] == 'Running':
            break

    req = requests.get(base_url + '%s/steps' % node['id'])
    print "Steps:\n", json.dumps(json.loads(req.text), indent=4)

main()
