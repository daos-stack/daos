#!/usr/bin/python
''' Experiments with BlueOcean REST API'''

import json
import os
import requests

def main():
    ''' main '''
    url_prefix = 'https://build.hpdd.intel.com/'
    base_url = url_prefix + 'blue/rest/organizations/' + \
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
    steps = json.loads(req.text)
    print "Steps:\n", json.dumps(steps, indent=4)

    step = None
    for step in steps:
        print step['id'], step['displayName'], step['state']
        if step['state'] == 'Running':
            break

    print "Node: {}, step: {}".format(node['id'], step['id'])

    for action in step['actions']:
        if action['urlName'] == 'log':
            req = requests.get(url_prefix + action['_links']['self']['href'] + \
                               '?start=0')
            break
    print "Log:\n", req.text

main()
