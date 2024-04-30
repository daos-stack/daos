#!/usr/bin/env python3

import argparse
import json
import re
import hashlib
import pandas as pd
from urllib.request import urlopen
from typing import List, Dict, Tuple, Any


PARSER = argparse.ArgumentParser()
PARSER.add_argument("--prefix", type=str, default='test')
PARSER.add_argument("--job", type=str, required=True)
PARSER.add_argument("--jid", type=str, required=True)


JENKINS_HOME = "https://build.hpdd.intel.com/job/daos-stack"


def je_load(pr: str, jid=None, what=None, tree=None):
    """Fetch something from Jenkins and return as native type."""
    url = f"{JENKINS_HOME}/job/daos/job/{pr}"
    if jid:
        url += f"/{jid}"
        if what:
            url += f"/{what}"
    url += "/api/json"
    if tree:
        url += f"?tree={tree}"
    with urlopen(url) as f:  # nosec
            return json.load(f)


def dump(data: Any, name: str) -> None:
    with open(f'{name}.json', 'w') as f:
        json.dump(data, f, indent=4)
    print(f'File has been written: {name}.json')


def tree_create(arts: List[str]) -> Dict[str, Any]:
    output = {}
    for art in arts:
        path = art.split('/')
        file = path[-1]
        path = path[0:-1]
        level = output
        for dir in path:
            if dir in level.keys():
                level = level[dir]
            else:
                level[dir] = {}
                level = level[dir]
        level[file] = 1
    return output


def html_step(arts: Dict, path: List[str]) -> str:
    dirs = []
    files = []
    for key in arts.keys():
        if arts[key] == 1:
            files.append(key)
        else:
            dirs.append(key)
    dirs.sort()
    files.sort()
    html = ''
    for dir in dirs:
        html += f'<li><span class="caret">{dir}</span>\n<ul class="nested">\n'
        html += html_step(arts[dir], path + [dir])
        html += '</ul>\n'
    for file in files:
        URL = '/'.join([JENKINS_HOME] + path + [file])
        html += f'<li><a href="{URL}">{file}</a></li>\n'
    return html

# https://www.w3schools.com/howto/howto_js_treeview.asp
HTML_PREFIX = '''
<html>
<head>
    <style>
        /* Remove default bullets */
        ul, #myUL {
        list-style-type: none;
        }

        /* Remove margins and padding from the parent ul */
        #myUL {
        margin: 0;
        padding: 0;
        }

        /* Style the caret/arrow */
        .caret {
        cursor: pointer;
        user-select: none; /* Prevent text selection */
        }

        /* Create the caret/arrow with a unicode, and style it */
        .caret::before {
        content: "\\25B6";
        color: black;
        display: inline-block;
        margin-right: 6px;
        }

        /* Rotate the caret/arrow icon when clicked on (using JavaScript) */
        .caret-down::before {
        transform: rotate(90deg);
        }

        /* Hide the nested list */
        .nested {
        display: none;
        }

        /* Show the nested list when the user clicks on the caret/arrow (with JavaScript) */
        .active {
        display: block;
        }
    </style>
</head
<body>
'''

HTML_SUFFIX = '''
    <script>
        var toggler = document.getElementsByClassName("caret");
        var i;

        for (i = 0; i < toggler.length; i++) {
            toggler[i].addEventListener("click", function() {
                this.parentElement.querySelector(".nested").classList.toggle("active");
                this.classList.toggle("caret-down");
            });
        }
    </script>
</body>
</html>
'''

def html_generate(arts: Dict, fname: str, path: List[str]) -> None:
    html = '<ul id="myUL">\n'
    html += html_step(arts, path)
    html += '</ul>\n'
    with open(f'{fname}.html', 'w') as f:
        f.write(HTML_PREFIX)
        f.write(html)
        f.write(HTML_SUFFIX)


def main():
    args = PARSER.parse_args()
    data = je_load(args.job, args.jid)
    arts = []
    for art in data['artifacts']:
        if not art['relativePath'].endswith(art['fileName']):
            print(art['relativePath'])
            exit(1)
        arts.append(art['relativePath'])
    arts = tree_create(arts)
    dump(arts, f'{args.prefix}_artifacts')
    html_generate(arts, f'{args.prefix}_artifacts',
                  ['job', 'daos', 'job', args.job, str(args.jid), 'artifact'])


if __name__ == "__main__":
    main()
