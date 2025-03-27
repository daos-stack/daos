#!/usr/bin/env python3

#
# Copyright(c) 2012-2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause-Clear
#

import shutil
import sys
import re
import os.path
import subprocess
import tests_config


def run_command(args, verbose=True):
    result = subprocess.run(" ".join(args), shell=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    result.stdout = result.stdout.decode("ASCII", errors='ignore')
    result.stderr = result.stderr.decode("ASCII", errors='ignore')
    if verbose:
        print(result.stderr)
    return result


#
# This script purpose is to remove unused functions definitions
# It is giving the opportunity to unit test all functions from OCF.
# As a parameter should be given path to file containing function,
# which is target of testing. However that file has to be after
# preprocessing.
#
# Output file of this script is not ready to make it. Before that,
# has to be given definitions of functions, which are used by
# tested function.
#
# In brief: this script allow wrapping all function calls in UT
#

class UnitTestsSourcesGenerator(object):
    script_file_abs_path = ""
    script_dir_abs_path = ""

    main_UT_dir = ""
    main_env_dir = ""
    main_tested_dir = ""

    ctags_path = ""

    test_catalogues_list = []
    dirs_to_include_list = []

    tests_internal_includes_list = []
    framework_includes = []

    dirs_with_tests_list = []
    test_files_paths_list = []

    tested_files_paths_list = []

    includes_to_copy_dict = {}

    preprocessing_repo = ""
    sources_to_test_repo = ""

    def __init__(self):
        self.script_file_abs_path = os.path.realpath(__file__)
        self.script_dir_abs_path = os.path.normpath(
            os.path.dirname(self.script_file_abs_path) + os.sep)

        self.set_ctags_path()

        self.set_main_UT_dir()
        self.set_main_env_dir()
        self.set_main_tested_dir()

        self.test_catalogues_list = tests_config.DIRECTORIES_WITH_TESTS_LIST
        self.set_includes_to_copy_dict(tests_config.INCLUDES_TO_COPY_DICT)
        self.set_dirs_to_include()

        self.set_tests_internal_includes_list()
        self.set_framework_includes()
        self.set_files_with_tests_list()
        self.set_tested_files_paths_list()

        self.set_preprocessing_repo()
        self.set_sources_to_test_repo()

    def preprocessing(self):
        tested_files_list = self.get_tested_files_paths_list()
        project_includes = self.get_dirs_to_include_list()
        framework_includes = self.get_tests_internal_includes_list()

        gcc_flags = " -fno-inline -Dstatic= -Dinline= -E "
        gcc_command_template = "gcc "
        for path in project_includes:
            gcc_command_template += " -I " + path + " "

        for path in framework_includes:
            gcc_command_template += " -I " + path

        gcc_command_template += gcc_flags

        for path in tested_files_list:
            preprocessing_dst = self.get_preprocessing_repo() \
                                + self.get_relative_path(path, self.get_main_tested_dir())
            preprocessing_dst_dir = os.path.dirname(preprocessing_dst)
            self.create_dir_if_not_exist(preprocessing_dst_dir)

            gcc_command = gcc_command_template + path + " > " + preprocessing_dst

            result = run_command([gcc_command])

            if result.returncode != 0:
                print(f"Generating preprocessing for {self.get_main_tested_dir() + path} failed!")
                print(result.output)
                run_command(["rm", "-f", preprocessing_dst])
                continue

            self.remove_hashes(preprocessing_dst)

            print(f"Preprocessed file {path} saved to {preprocessing_dst}")

    def copy_includes(self):
        includes_dict = self.get_includes_to_copy_dict()

        for dst, src in includes_dict.items():
            src_path = os.path.normpath(self.get_main_tested_dir() + src)
            if not os.path.isdir(src_path):
                print(f"Directory {src_path} given to include does not exists!")
                continue
            dst_path = os.path.normpath(self.get_main_UT_dir() + dst)

            shutil.rmtree(dst_path, ignore_errors=True)
            shutil.copytree(src_path, dst_path)

    def get_user_wraps(self, path):
        functions_list = self.get_functions_list(path)
        functions_list = [re.sub(r'__wrap_([\S]+)\s*[\d]+', r'\1', line)
                          for line in functions_list if re.search("__wrap_", line)]

        return functions_list

    def get_autowrap_file_path(self, test_file_path):
        wrap_file_path = test_file_path.rsplit('.', 1)[0]
        wrap_file_path = wrap_file_path + "_generated_wraps.c"
        return wrap_file_path

    def is_env_function(self, name):
        prefixes = ["env", "bug", "memcpy", "memset", "memcmp", "strnlen", "strncmp", "strncpy"]

        return list(filter(name.startswith, prefixes)) != []

    def prepare_autowraps(self, test_file_path, preprocessed_file_path):
        functions_to_wrap = self.get_functions_calls(
            self.get_sources_to_test_repo() + test_file_path)
        user_wraps = set(self.get_user_wraps(self.get_main_UT_dir() + test_file_path))

        functions_to_wrap = functions_to_wrap - user_wraps

        tags_list = self.get_functions_list(preprocessed_file_path, prototypes=True)

        wrap_list = []

        with open(preprocessed_file_path) as f:
            code = f.readlines()
            for function in functions_to_wrap:
                if self.is_env_function(function):
                    continue
                for tag in tags_list:
                    if function in tag:
                        name, line = tag.split()
                        if name == function:
                            line = int(line)
                            wrap_list.append(self.get_function_wrap(code, line))
                            break

        wrap_file_path = self.get_main_UT_dir() + self.get_autowrap_file_path(test_file_path)

        with open(wrap_file_path, "w") as f:
            f.write("/* This file is generated by UT framework */\n")
            for wrap in wrap_list:
                f.write(wrap + "\n")

    def prepare_sources_for_testing(self):
        test_files_paths = self.get_files_with_tests_list()

        for test_path in test_files_paths:
            path = self.get_tested_file_path(self.get_main_UT_dir() + test_path)

            preprocessed_tested_path = self.get_preprocessing_repo() + path
            if not os.path.isfile(preprocessed_tested_path):
                print(f"No preprocessed path for {test_path} test file.")
                continue

            tested_src = self.get_src_to_test(test_path, preprocessed_tested_path)

            self.create_dir_if_not_exist(
                self.get_sources_to_test_repo() + os.path.dirname(test_path))

            with open(self.get_sources_to_test_repo() + test_path, "w") as f:
                f.writelines(tested_src)
                print(
                    f"Sources for {test_path} saved in + \
                    {self.get_sources_to_test_repo() + test_path}")

            self.prepare_autowraps(test_path, preprocessed_tested_path)

    def create_main_cmake_lists(self):
        buf = "cmake_minimum_required(VERSION 2.6.0)\n\n"
        buf += "project(OCF_unit_tests C)\n\n"

        buf += "enable_testing()\n\n"

        buf += "include_directories(\n"
        dirs_to_inc = self.get_dirs_to_include_list() + self.get_framework_includes() \
            + self.get_tests_internal_includes_list()
        for path in dirs_to_inc:
            buf += "\t" + path + "\n"
        buf += ")\n\n"

        includes = self.get_tests_internal_includes_list()
        for path in includes:
            buf += "\nadd_subdirectory(" + path + ")"
        buf += "\n\n"

        test_files = self.get_files_with_tests_list()
        test_dirs_to_include = [os.path.dirname(path) for path in test_files]

        test_dirs_to_include = self.remove_duplicates_from_list(test_dirs_to_include)

        for path in test_dirs_to_include:
            buf += "\nadd_subdirectory(" + self.get_sources_to_test_repo() + path + ")"

        with open(self.get_main_UT_dir() + "CMakeLists.txt", "w") as f:
            f.writelines(buf)

        print(f"Main CMakeLists.txt generated written to {self.get_main_UT_dir()} CMakeLists.txt")

    def generate_cmakes_for_tests(self):
        test_files_paths = self.get_files_with_tests_list()

        for test_path in test_files_paths:
            tested_file_path = self.get_sources_to_test_repo() + test_path
            if not os.path.isfile(tested_file_path):
                print(f"No source to test for {test_path} test")
                continue

            test_file_path = self.get_main_UT_dir() + test_path

            cmake_buf = self.generate_test_cmake_buf(test_file_path, tested_file_path)

            cmake_path = self.get_sources_to_test_repo() + test_path
            cmake_path = os.path.splitext(cmake_path)[0] + ".cmake"
            with open(cmake_path, "w") as f:
                f.writelines(cmake_buf)
                print(f"cmake file for {test_path} written to {cmake_path}")

            cmake_lists_path = os.path.dirname(cmake_path) + os.sep
            self.update_cmakelists(cmake_lists_path, cmake_path)

    def generate_test_cmake_buf(self, test_file_path, tested_file_path):
        test_file_name = os.path.basename(test_file_path)
        target_name = os.path.splitext(test_file_name)[0]

        add_executable = "add_executable(" + target_name + " " + test_file_path + " " + \
                         tested_file_path + ")\n"

        libraries = "target_link_libraries(" + target_name + "  libcmocka.so ocf_env)\n"

        add_test = "add_test(" + target_name + " ${CMAKE_CURRENT_BINARY_DIR}/" + target_name + ")\n"

        tgt_properties = "set_target_properties(" + target_name + "\n" + \
                         "PROPERTIES\n" + \
                         "COMPILE_FLAGS \"-fno-inline -Dstatic= -Dinline= -w \"\n"

        link_flags = self.generate_cmake_link_flags(test_file_path)
        tgt_properties += link_flags + ")"

        buf = add_executable + libraries + add_test + tgt_properties

        return buf

    def generate_cmake_link_flags(self, path):
        ret = ""

        autowraps_path = self.get_autowrap_file_path(path)
        functions_to_wrap = self.get_functions_to_wrap(path)
        functions_to_wrap += self.get_functions_to_wrap(autowraps_path)

        for function_name in functions_to_wrap:
            ret += ",--wrap=" + function_name
        if len(ret) > 0:
            ret = "LINK_FLAGS \"-Wl" + ret + "\"\n"

        return ret

    def update_cmakelists(self, cmake_lists_path, cmake_name):
        with open(cmake_lists_path + "CMakeLists.txt", "a+") as f:
            f.seek(0, os.SEEK_SET)
            new_line = "include(" + os.path.basename(cmake_name) + ")\n"

            if new_line not in f.read():
                f.write(new_line)

    def get_functions_to_wrap(self, path):
        functions_list = self.get_functions_list(path)
        functions_list = [re.sub(r'__wrap_([\S]+)\s*[\d]+', r'\1', line) for line in functions_list
                          if re.search("__wrap_", line)]

        return functions_list

    def get_functions_to_leave(self, path):
        with open(path) as f:
            lines = f.readlines()
            buf = ''.join(lines)

        tags_pattern = re.compile(r"<functions_to_leave>[\s\S]*</functions_to_leave>")

        buf = re.findall(tags_pattern, buf)
        if not len(buf) > 0:
            return []

        buf = buf[0]

        buf = re.sub(r'<.*>', '', buf)
        buf = re.sub(r'[^a-zA-Z0-9_\n]+', '', buf)

        ret = buf.split("\n")
        ret = [name for name in ret if name]
        return ret

    def get_functions_list(self, file_path, prototypes=None):
        ctags_path = self.get_ctags_path()

        ctags_args = "--c-types=f"
        if prototypes:
            ctags_args += " --c-kinds=+p"
        # find all functions' definitions | put tabs instead of spaces |
        # take only columns with function name and line number | sort in descending order
        result = run_command([ctags_path, "-x", ctags_args, file_path,
                              "--language-force=c | sed \"s/ \\+/\t/g\" | cut -f 1,3 | sort -nsr "
                              "-k 2"])

        # 'output' is string, but it has to be changed to list
        output = list(filter(None, result.stdout.split("\n")))
        return output

    def remove_functions_from_list(self, functions_list, to_remove_list):
        ret = functions_list[:]
        for function_name in to_remove_list:
            ret = [line for line in ret if not re.search(r'\b%s\b' % function_name, line)]
        return ret

    def get_src_to_test(self, test_path, preprocessed_tested_path):
        functions_to_leave = self.get_functions_to_leave(self.get_main_UT_dir() + test_path)

        functions_to_leave.append(self.get_tested_function_name(self.get_main_UT_dir() + test_path))
        functions_list = self.get_functions_list(preprocessed_tested_path)

        functions_list = self.remove_functions_from_list(functions_list, functions_to_leave)

        with open(preprocessed_tested_path) as f:
            ret = f.readlines()
            for function in functions_list:
                line = function.split("\t")[1]
                line = int(line)

                self.remove_function_body(ret, line)

            return ret

    def set_tested_files_paths_list(self):
        test_files_list = self.get_files_with_tests_list()

        for f in test_files_list:
            self.tested_files_paths_list.append(self.get_main_tested_dir()
                                                + self.get_tested_file_path(
                                                self.get_main_UT_dir() + f))

        self.tested_files_paths_list = self.remove_duplicates_from_list(
            self.tested_files_paths_list)

    def get_tested_files_paths_list(self):
        return self.tested_files_paths_list

    def get_files_with_tests_list(self):
        return self.test_files_paths_list

    def set_files_with_tests_list(self):
        test_catalogues_list = self.get_tests_catalouges_list()
        for catalogue in test_catalogues_list:
            dir_with_tests_path = self.get_main_UT_dir() + catalogue

            for path, dirs, files in os.walk(dir_with_tests_path):
                test_files = self.get_test_files_from_dir(path + os.sep)

                for test_file_name in test_files:
                    test_rel_path = os.path.relpath(path + os.sep + test_file_name,
                                                    self.get_main_UT_dir())
                    self.test_files_paths_list.append(test_rel_path)

    def are_markups_valid(self, path):
        file_path = self.get_tested_file_path(path)
        function_name = self.get_tested_function_name(path)

        if file_path is None:
            print(f"{path} file has no tested_file tag!")
            return None
        elif not os.path.isfile(self.get_main_tested_dir() + file_path):
            print(f"Tested file given in {path} does not exist!")
            return None

        if function_name is None:
            print(f"{path} file has no tested_function_name tag!")
            return None

        return True

    def create_dir_if_not_exist(self, path):
        if not os.path.isdir(path):
            try:
                os.makedirs(path)
            except Exception:
                pass
            return True
        return None

    def get_tested_file_path(self, test_file_path):
        with open(test_file_path) as f:
            buf = f.readlines()
            buf = ''.join(buf)

        tags_pattern = re.compile(r"<tested_file_path>[\s\S]*</tested_file_path>")
        buf = re.findall(tags_pattern, buf)

        if not len(buf) > 0:
            return None

        buf = buf[0]

        buf = re.sub(r'<[^>]*>', '', buf)
        buf = re.sub(r'\s+', '', buf)

        if len(buf) > 0:
            return buf

        return None

    def get_tested_function_name(self, test_file_path):
        with open(test_file_path) as f:
            buf = f.readlines()
            buf = ''.join(buf)

        tags_pattern = re.compile(r"<tested_function>[\s\S]*</tested_function>")
        buf = re.findall(tags_pattern, buf)

        if not len(buf) > 0:
            return None

        buf = buf[0]

        buf = re.sub(r'<[^>]*>', '', buf)
        buf = re.sub('//', '', buf)
        buf = re.sub(r'\s+', '', buf)

        if len(buf) > 0:
            return buf

        return None

    def get_test_files_from_dir(self, path):
        ret = os.listdir(path)
        ret = [name for name in ret if os.path.isfile(path + os.sep + name)
               and (name.endswith(".c") or name.endswith(".h"))]
        ret = [name for name in ret if self.are_markups_valid(path + name)]

        return ret

    def get_list_of_directories(self, path):
        if not os.path.isdir(path):
            return []

        ret = os.listdir(path)
        ret = [name for name in ret if not os.path.isfile(path + os.sep + name)]
        ret = [os.path.normpath(name) + os.sep for name in ret]

        return ret

    def remove_hashes(self, path):
        with open(path) as f:
            buf = f.readlines()

        buf = [l for l in buf if not re.search(r'.*#.*', l)]

        with open(path, "w") as f:
            f.writelines(buf)

        return
        for i in range(len(padding)):
            try:
                padding[i] = padding[i].split("#")[0]
            except ValueError:
                continue

        f = open(path, "w")
        f.writelines(padding)
        f.close()

    def find_function_end(self, code_lines_list, first_line_of_function_index):
        brackets_counter = 0
        current_line_index = first_line_of_function_index

        while True:
            if "{" in code_lines_list[current_line_index]:
                brackets_counter += code_lines_list[current_line_index].count("{")
                brackets_counter -= code_lines_list[current_line_index].count("}")
                break
            else:
                current_line_index += 1

        while brackets_counter > 0:
            current_line_index += 1
            if "{" in code_lines_list[current_line_index]:
                brackets_counter += code_lines_list[current_line_index].count("{")
                brackets_counter -= code_lines_list[current_line_index].count("}")
            elif "}" in code_lines_list[current_line_index]:
                brackets_counter -= code_lines_list[current_line_index].count("}")

        return current_line_index

    def get_functions_calls(self, file_to_compile):
        out_dir = "/tmp/ocf_ut"
        out_file = out_dir + "/ocf_obj.o"
        self.create_dir_if_not_exist(out_dir)
        cmd = "/usr/bin/gcc -o " + out_file + " -c " + file_to_compile + " &> /dev/null"
        run_command([cmd], verbose=None)
        result = run_command(["/usr/bin/nm -u " + out_file + " | cut -f2 -d\'U\'"])
        return set(result.stdout.split())

    def remove_function_body(self, code_lines_list, line_id):
        try:
            while "{" not in code_lines_list[line_id]:
                if ";" in code_lines_list[line_id]:
                    return
                line_id += 1
        except IndexError:
            return

        last_line_id = self.find_function_end(code_lines_list, line_id)

        code_lines_list[line_id] = code_lines_list[line_id].split("{")[0]
        code_lines_list[line_id] += ";"

        del code_lines_list[line_id + 1: last_line_id + 1]

    def get_function_wrap(self, code_lines_list, line_id):
        ret = []
        # Line numbering starts with one, list indexing with zero
        line_id -= 1

        # If returned type is not present, it should be in line above
        try:
            code_lines_list[line_id].split("(")[0].rsplit()[1]
        except IndexError:
            line_id -= 1

        while True:
            ret.append(code_lines_list[line_id])
            if ")" in code_lines_list[line_id]:
                break
            line_id += 1

        # Tags list contains both prototypes and definitions, here we recoginze
        # with which one we deals
        delimiter = ""
        try:
            if "{" in ret[-1] or "{" in ret[-2]:
                delimiter = "{"
            else:
                delimiter = ";"
        except IndexError:
            delimiter = ";"

        ret[-1] = ret[-1].split(delimiter)[0]
        ret[-1] += "{}"

        function_name = ""
        line_with_name = 0
        try:
            function_name = ret[line_with_name].split("(")[0].rsplit(maxsplit=1)[1]
        except IndexError:
            line_with_name = 1
            function_name = ret[line_with_name].split("(")[0]

        function_new_name = "__wrap_" + function_name.replace("*", "")
        ret[0] = ret[0].replace(function_name, function_new_name)

        return ''.join(ret)

    def set_ctags_path(self):
        result = run_command(["/usr/bin/ctags --version &> /dev/null"])
        if result.returncode == 0:
            path = "/usr/bin/ctags "
            result = run_command([path, "--c-types=f"], verbose=None)
            if not re.search("unrecognized option", result.stdout, re.IGNORECASE):
                self.ctags_path = path
                return

        result = run_command(["/usr/local/bin/ctags --version &> /dev/null"])
        if result.returncode == 0:
            path = "/usr/local/bin/ctags "
            result = run_command(["path", "--c-types=f"], verbose=None)
            if not re.search("unrecognized option", result.stdout, re.IGNORECASE):
                self.ctags_path = path
                return

        print("ERROR: Current ctags version don't support \"--c-types=f\" parameter!")
        exit(1)

    def get_ctags_path(self):
        return self.ctags_path

    def get_tests_catalouges_list(self):
        return self.test_catalogues_list

    def get_relative_path(self, original_path, part_to_remove):
        return original_path.split(part_to_remove, 1)[1]

    def get_dirs_to_include_list(self):
        return self.dirs_to_include_list

    def set_dirs_to_include(self):
        self.dirs_to_include_list = [self.get_main_tested_dir() + name
                                     for name in
                                     tests_config.DIRECTORIES_TO_INCLUDE_FROM_PROJECT_LIST]

    def set_tests_internal_includes_list(self):
        self.tests_internal_includes_list = [self.get_main_UT_dir() + name
                                             for name in
                                             tests_config.DIRECTORIES_TO_INCLUDE_FROM_UT_LIST]

    def set_preprocessing_repo(self):
        self.preprocessing_repo = self.get_main_UT_dir() \
                                  + tests_config.PREPROCESSED_SOURCES_REPOSITORY

    def set_sources_to_test_repo(self):
        self.sources_to_test_repo = self.get_main_UT_dir() + tests_config.SOURCES_TO_TEST_REPOSITORY

    def get_sources_to_test_repo(self):
        return self.sources_to_test_repo

    def get_preprocessing_repo(self):
        return self.preprocessing_repo

    def get_tests_internal_includes_list(self):
        return self.tests_internal_includes_list

    def get_script_dir_path(self):
        return os.path.normpath(self.script_dir_abs_path) + os.sep

    def get_main_UT_dir(self):
        return os.path.normpath(self.main_UT_dir) + os.sep

    def get_main_env_dir(self):
        return os.path.normpath(self.main_env_dir) + os.sep

    def get_main_tested_dir(self):
        return os.path.normpath(self.main_tested_dir) + os.sep

    def remove_duplicates_from_list(self, l):
        return list(set(l))

    def set_framework_includes(self):
        self.framework_includes = tests_config.FRAMEWORK_DIRECTORIES_TO_INCLUDE_LIST

    def get_framework_includes(self):
        return self.framework_includes

    def set_includes_to_copy_dict(self, files_to_copy_dict):
        self.includes_to_copy_dict = files_to_copy_dict

    def get_includes_to_copy_dict(self):
        return self.includes_to_copy_dict

    def set_main_env_dir(self):
        main_env_dir = os.path.normpath(os.path.normpath(self.get_script_dir_path()
                                                         + os.sep
                                                         + tests_config.
                                                         MAIN_DIRECTORY_OF_ENV_FILES))
        if not os.path.isdir(main_env_dir):
            print("Given path to main env directory is wrong!")
            sys.exit(1)

        self.main_env_dir = main_env_dir

    def set_main_UT_dir(self):
        main_UT_dir = os.path.normpath(os.path.normpath(self.get_script_dir_path()
                                                        + os.sep
                                                        + tests_config.
                                                        MAIN_DIRECTORY_OF_UNIT_TESTS))
        if not os.path.isdir(main_UT_dir):
            print("Given path to main UT directory is wrong!")
            sys.exit(1)

        self.main_UT_dir = main_UT_dir

    def set_main_tested_dir(self):
        main_tested_dir = os.path.normpath(os.path.normpath(self.get_script_dir_path()
                                                            + os.sep
                                                            + tests_config.
                                                            MAIN_DIRECTORY_OF_TESTED_PROJECT))
        if not os.path.isdir(main_tested_dir):
            print("Given path to main tested directory is wrong!")
            sys.exit(1)

        self.main_tested_dir = main_tested_dir


def __main__():
    generator = UnitTestsSourcesGenerator()
    generator.copy_includes()
    generator.preprocessing()
    generator.prepare_sources_for_testing()
    generator.create_main_cmake_lists()
    generator.generate_cmakes_for_tests()

    print("Files for testing generated!")


if __name__ == "__main__":
    __main__()
