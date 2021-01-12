/*
 * (C) Copyright 2018-2020 Intel Corporation.
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

package io.daos;

import io.daos.dfs.StatAttributes;
import org.apache.commons.lang.StringUtils;

import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.UUID;

/**
 * Utility class.
 */
public final class DaosUtils {

  public static final Pattern PAT_FILENAME = Pattern.compile(
          "^[\\x{0}-\\x{2e},\\x{30}-\\x{ff}]{1," + Constants.FILE_NAME_LEN_MAX + "}$");

  private DaosUtils() {
  }

  /**
   * normalize path to make sure it's valid path.
   *
   * @param path
   * path string
   * @return String
   */
  public static String normalize(String path) {
    if (path == null || (path = path.trim()).length() == 0) {
      return "";
    }
    path = path.replaceAll("\\\\{1,}", "/");
    path = path.replaceAll("/{2,}", "/");

    if (path.length() > Constants.FILE_PATH_LEN_MAX) {
      throw new IllegalArgumentException("path length should not exceed " + Constants.FILE_PATH_LEN_MAX);
    }

    String[] paths = path.split("/");
    for (String p : paths) {
      if (p == null || p.length() == 0) {
        continue;
      }
      Matcher m = PAT_FILENAME.matcher(p);
      if (!m.matches()) {
        throw new IllegalArgumentException("Invalid file name. " +
                "only characters with hexadecimal [x00 to xff] are valid. max length is " +
                Constants.FILE_NAME_LEN_MAX);
      }
    }
    if (path.length() > 1 && path.endsWith("/")) {
      path = path.substring(0, path.length() - 1);
    }
    return path;
  }

  /**
   * split parent and name.
   *
   * @param path
   * path string
   * @return String[]
   */
  public static String[] parsePath(String path) {
    int slash = path.lastIndexOf('/');
    if (slash >= 0 && path.length() > 1) {
      return new String[]{path.substring(0, slash), path.substring(slash + 1)};
    }
    return new String[]{path};
  }

  /**
   * random UUID.
   * @return UUID in string
   */
  public static String randomUUID() {
    String id = UUID.randomUUID().toString();
    return id.substring(0, 16);
  }

  /**
   * convert C TimeSpec to time in milliseconds.
   * @param timeSpec
   * C TimeSpec
   * @return time in milliseconds
   */
  public static long toMilliSeconds(StatAttributes.TimeSpec timeSpec) {
    long ms = timeSpec.getSeconds() * 1000;
    return ms + timeSpec.getNano() / (1000 * 1000);
  }

  /**
   * escape UNS value to make it valid in command line.
   *
   * @param value
   * value to be escaped
   * @return escaped value
   */
  public static String escapeUnsValue(String value) {
    if (StringUtils.isBlank(value)) {
      return value;
    }
    value = value.replaceAll(":", "\\\\u003a");
    return value.replaceAll("=", "\\\\u003d");
  }
}
