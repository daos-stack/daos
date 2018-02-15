#!/usr/bin/env python
#
#   go.py
#   SCons Go Tools
#
#   Copyright (c) 2010, Ross Light.
#   Copyright (C) 2018, Intel Corporation.
#   All rights reserved.
#
#   Redistribution and use in source and binary forms, with or without
#   modification, are permitted provided that the following conditions are met:
#
#       Redistributions of source code must retain the above copyright notice,
#       this list of conditions and the following disclaimer.
#
#       Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#
#       Neither the name of the SCons Go Tools nor the names of its contributors
#       may be used to endorse or promote products derived from this software
#       without specific prior written permission.
#
#   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#   ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
#   LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
#   CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
#   SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
#   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
#   CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
#   ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
#   POSSIBILITY OF SUCH DAMAGE.
#
"""SCons Tool for building Go Programs"""

import os
import posixpath
import re
import subprocess

# pylint: disable=no-name-in-module
# pylint: disable=import-error
from SCons.Action import Action
from SCons.Scanner import Scanner
from SCons.Builder import Builder
# pylint: enable=no-name-in-module
# pylint: enable=import-error

def _subdict(src, keys):
    """Create a dict with keys from source dict"""
    result = {}
    for key in keys:
        try:
            result[key] = src[key]
        except KeyError:
            pass
    return result

# PLATFORMS

_VALID_PLATFORMS = frozenset((
    ('darwin', '386'),
    ('darwin', 'amd64'),
    ('freebsd', '386'),
    ('freebsd', 'amd64'),
    ('linux', '386'),
    ('linux', 'amd64'),
    ('linux', 'arm'),
    ('nacl', '386'),
    ('windows', '386'),
))
_ARCHS = {'amd64': '6', '386': '8', 'arm': '5'}

def _get_platform_info(env, goos, goarch):
    """Get information about go"""
    info = {}
    if (goos, goarch) not in _VALID_PLATFORMS:
        raise ValueError("Unrecognized platform: %s, %s" % (goos, goarch))
    info['archname'] = _ARCHS[goarch]
    info['pkgroot'] = os.path.join(env['ENV']['GOROOT'],
                                   'pkg', goos + '_' + goarch)
    return info

def _get_host_platform(env):
    """Get information about host"""
    newenv = env.Clone()
    newenv['ENV'].pop('GOOS', None)
    newenv['ENV'].pop('GOARCH', None)
    newenv['ENV'].pop('GOBIN', None)
    config = newenv.Configure()
    if not config.CheckProg('go'):
        print "go is required"
        newenv.Exit(2)
    config.Finish()
    config = _parse_config(_run_goenv(newenv))
    env.Append(ENV={'GOROOT' : config['GOROOT'],
                    'GOBIN' : os.path.join(config['GOROOT'], 'bin')})
    return config['GOOS'], config['GOARCH']

# COMPILER

_PACKAGE_PAT = re.compile(r'package\s+(\w+)\s*;?', re.UNICODE)
_SPEC_PAT = re.compile(r'\s*(\.|\w+)?\s*\"(.*?)\"\s*;?', re.UNICODE)
def _get_imports(node):
    """Scan go file for imports"""
    source = node.get_text_contents()
    while source:
        source = source.lstrip()
        if source.startswith('//'):
            source = _after_token(source, '\n')
        elif source.startswith('/*'):
            source = _after_token(source, '*/')
        elif source.startswith('package') and source[7].isspace():
            match = _PACKAGE_PAT.match(source)
            if not match:
                return
            source = source[match.end():]
        elif source.startswith('import') and not source[6].isalnum():
            source = source[6:].lstrip()
            if source.startswith('('):
                # Compound import
                source = source[1:]
                while True:
                    match = _SPEC_PAT.match(source)
                    if match:
                        yield match.group(2)
                        source = source[match.end():]
                    else:
                        break
                source = source.lstrip()
                if not source.startswith(')'):
                    return
                source = source[1:]
            else:
                # Single import
                match = _SPEC_PAT.match(source)
                if match:
                    yield match.group(2)
                    source = source[match.end():]
                else:
                    return
        else:
            #Once we see any other statement, the imports are done
            return

def _after_token(string, tok):
    """Helper function"""
    try:
        return string[string.index(tok) + len(tok):]
    except ValueError:
        return ''

def _go_scan_func(node, env, _paths):
    """Go file scanner"""
    package_paths = env['GO_LIBPATH'] + [env['GO_PKGROOT']]
    result = []
    for package_name in _get_imports(node):
        if package_name.startswith("./"):
            result.append(env.File(package_name + _go_object_suffix(env, [])))
            continue
        # Search for import
        package_dir, package_name = posixpath.split(package_name)
        subpaths = [posixpath.join(p, package_dir) for p in package_paths]
        # Check for a static library
        package = env.FindFile(
            package_name + os.path.extsep + 'a',
            subpaths,
        )
        if package is not None:
            result.append(package)
            continue
        # Check for a build result
        package = env.FindFile(
            package_name + os.path.extsep + env['GO_ARCHNAME'],
            subpaths,
        )
        if package is not None:
            result.append(package)
            continue
    return result

GO_SCANNER = Scanner(function=_go_scan_func, skeys=['.go'])

def _gc_emitter(target, source, env):
    """Go emitter"""
    if env['GO_STRIPTESTS']:
        return (target, [s for s in source if not str(s).endswith('_test.go')])
    return (target, source)

def _ld_scan_func(node, env, _path):
    """Go linker scan function"""
    obj_suffix = os.path.extsep + env['GO_ARCHNAME']
    result = []
    for child in node.children():
        if str(child).endswith(obj_suffix) or str(child).endswith('.a'):
            result.append(child)
    return result

def _go_object_suffix(env, _sources):
    """Go object suffix"""
    return os.path.extsep + env['GO_ARCHNAME']

def _go_program_prefix(env, _sources):
    """Go program prefix"""
    return env['PROGPREFIX']

def _go_program_suffix(env, _sources):
    """Go program suffix"""
    return env['PROGSUFFIX']

GO_COMPILER = Builder(
    action=Action('$GO_GCCOM', '$GO_GCCOMSTR'),
    emitter=_gc_emitter,
    suffix=_go_object_suffix,
    ensure_suffix=True,
    src_suffix='.go',
)
GO_LINKER = Builder(
    action=Action('$GO_LDCOM', '$GO_LDCOMSTR'),
    prefix=_go_program_prefix,
    suffix=_go_program_suffix,
    src_builder=GO_COMPILER,
    single_source=True,
    source_scanner=Scanner(function=_ld_scan_func, recursive=True),
)
GO_ASSEMBLER = Builder(
    action=Action('$GO_ACOM', '$GO_ACOMSTR'),
    suffix=_go_object_suffix,
    ensure_suffix=True,
    src_suffix='.s',
)
GOPACK = Builder(
    action=Action('$GO_PACKCOM', '$GO_PACKCOMSTR'),
    suffix='.a',
    ensure_suffix=True,
)

## CONFIGURATION

def _parse_config(data):
    """Parse the go config"""
    result = {}
    for line in data.splitlines():
        name, value = line.split('=', 1)
        if name.startswith('export '):
            name = name[len('export '):]
        result[name] = value.strip('"')
    return result

def _run_goenv(env):
    """Run go env"""
    binary = "go"
    binpath = env["ENV"].get("GOROOT", None)
    if binpath:
        binary = os.path.join(binpath, "bin", "go")
    proc = subprocess.Popen(
        [binary, 'env'],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    stdout, _stderr = proc.communicate()
    return stdout

## TESTING

def _get_package_info(env, node):
    """Get go package info"""
    package_name = os.path.splitext(node.name)[0]
    # Find import path
    for path in env['GO_LIBPATH'] + [env['GO_PKGROOT']]:
        search_dir = env.Dir(path)
        if node.is_under(search_dir):
            return package_name, os.path.splitext(search_dir.rel_path(node))[0]
    # Try under launch directory as a last resort
    search_dir = env.Dir(env.GetLaunchDir())
    if node.is_under(search_dir):
        return package_name, "./" + \
            os.path.splitext(search_dir.rel_path(node))[0]
    else:
        raise ValueError("Package %s not found in lib path" % (package_name))

def _read_func_names(filep):
    """read function names"""
    magic = '\tfunc "".'
    started = False
    pkg = ''
    for line in filep:
        if started:
            if line.startswith(magic):
                yield (pkg, line[len(magic):line.index(' ', len(magic))])
            elif line.lstrip().startswith('package'):
                pkg = line.split()[1]
            elif line.startswith('$$'):
                # We only want the first section
                break
        elif line.startswith('$$'):
            started = True
    filep.close()

def gotest(target, source, env):
    """compile tests"""
    # Compile test information
    import_list = [[_get_package_info(env, snode)[1],
                    False] for snode in source]
    tests = []
    benchmarks = []
    for i, snode in enumerate(source):
        proc = None
        # Start reading functions
        if str(snode).endswith('.a'):
            proc = subprocess.Popen([env['GO_PACK'], 'p',
                                     str(snode), '__.PKGDEF'],
                                    stdout=subprocess.PIPE)
            names = _read_func_names(proc.stdout)
        else:
            names = _read_func_names(open(str(snode)))
        # Handle names
        for (package, ident) in names:
            name = package + '.' + ident
            info = (i, ident, name)
            if ident.startswith('Test'):
                tests.append(info)
                import_list[i][1] = True # mark as used
            elif ident.startswith('Bench'):
                benchmarks.append(info)
                import_list[i][1] = True # mark as used
        # Wait on GOPACK subprocess
        if proc is not None:
            proc.wait()
            if proc.returncode != 0:
                return proc.returncode
    # Write out file
    filep = open(str(target[0]), 'w')
    try:
        filep.write("package main\n\n")
        # Imports
        filep.write("import \"testing\"\n")
        filep.write("import __regexp__ \"regexp\"\n")
        filep.write("import (\n")
        for i, (import_path, used) in enumerate(import_list):
            if used:
                filep.write("\tt%04d \"%s\"\n" % (i, import_path))
        filep.write(")\n\n")
        # Test array
        filep.write("var tests = []testing.InternalTest{\n")
        for pkg_num, ident, name in tests:
            filep.write("\ttesting.InternalTest{\"%s\", t%04d.%s},\n" % \
                (name, pkg_num, ident))
        filep.write("}\n\n")
        # Benchmark array
        filep.write("var benchmarks = []testing.InternalBenchmark{\n")
        for pkg_num, ident, name in benchmarks:
            filep.write("\ttesting.InternalBenchmark{\"%s\", t%04d.%s},\n" % \
                (name, pkg_num, ident))
        filep.write("}\n\n")
        # Main function
        filep.write("func main() {\n")
        filep.write("\ttesting.Main(__regexp__.MatchString, tests, ")
        filep.write("benchmarks)\n")
        filep.write("}\n")
    finally:
        filep.close()
    return 0

GO_TESTER = Builder(
    action=Action(gotest, '$GO_TESTCOMSTR'),
    suffix='.go',
    ensure_suffix=True,
    src_suffix=_go_object_suffix,
)

# API

def go_target(env, goos, goarch):
    """Setup go"""
    config = _get_platform_info(env, goos, goarch)
    env['ENV']['GOOS'] = goos
    env['ENV']['GOARCH'] = goarch
    env['GO_GC'] = 'go tool compile'
    env['GO_LD'] = 'go tool link'
    env['GO_A'] = 'go tool asm'
    env['GO_PACK'] = 'go tool pack'
    env['GO_ARCHNAME'] = config['archname']
    env['GO_PKGROOT'] = config['pkgroot']

def generate(env):
    """Setup the go tools"""
    if 'HOME' not in env['ENV']:
        env['ENV']['HOME'] = os.environ['HOME']
    # Set up tools
    env.AddMethod(go_target, 'GoTarget')
    env.Append(ENV=_subdict(os.environ, ['GOROOT', 'GOBIN']))
    goos, goarch = _get_host_platform(env)
    env.GoTarget(goos, goarch)
    # Add builders and scanners
    gccom = '$GO_GC -o $TARGET ${_concat("-I ", GO_LIBPATH, "", __env__)}'
    gccom += ' $GO_GCFLAGS $SOURCES'
    ldcom = '$GO_LD -o $TARGET ${_concat("-L ", GO_LIBPATH, "", __env__)}'
    ldcom += ' $GO_LDFLAGS $SOURCE'
    env.Append(
        BUILDERS={
            'Go': GO_COMPILER,
            'GoProgram': GO_LINKER,
            'GoAssembly': GO_ASSEMBLER,
            'GoPack': GOPACK,
            'GoTest': GO_TESTER,
        },
        SCANNERS=[GO_SCANNER],
        GO_GCCOM=gccom,
        GO_LDCOM=ldcom,
        GO_ACOM='$GO_A -o $TARGET $SOURCE',
        GO_PACKCOM='rm -f $TARGET ; $GO_PACK gcr $TARGET $SOURCES',
        GO_LIBPATH=[],
        GO_STRIPTESTS=False,
    )

def exists(_env):
    """assert existence of tool"""
    return True
