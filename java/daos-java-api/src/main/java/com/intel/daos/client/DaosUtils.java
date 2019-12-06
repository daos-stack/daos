/*
 * (C) Copyright 2018-2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

package com.intel.daos.client;

import java.util.UUID;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Utility class
 */
public final class DaosUtils {

  public static final Pattern PAT_PATH = Pattern.compile("^(/|(/[a-zA-Z0-9_\\.-]+)|[a-zA-Z0-9_\\.-]+)+$");

  private DaosUtils(){}

  /**
   * normalize path to make sure it's valid path
   * @param path
   * @return
   */
  public static String normalize(String path){
    if(path == null || (path=path.trim()).length() == 0){
      return "";
    }
    path = path.replaceAll("\\\\{1,}", "/");
    Matcher m = PAT_PATH.matcher(path);
    if(!m.matches()){
      throw new IllegalArgumentException("Invalid path. only characters / a-z A-Z 0-9 _ - . are valid");
    }
    if(path.length() > 1 && path.endsWith("/")){
      path = path.substring(0, path.length()-1);
    }
    return path;
  }

  /**
   * split parent and name
   * @param path
   * @return
   */
  public static String[] parsePath(String path) {
    int slash = path.lastIndexOf('/');
    if(slash >= 0 && path.length()>1){
      return new String[] {path.substring(0, slash), path.substring(slash+1)};
    }
    return new String[] {path};
  }

  public static String randomUUID() {
    String id = UUID.randomUUID().toString();
    return id.substring(0, 16);
  }
}
