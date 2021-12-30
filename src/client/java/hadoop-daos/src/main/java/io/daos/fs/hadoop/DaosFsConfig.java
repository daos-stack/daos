/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.lang.reflect.Field;
import java.net.URI;
import java.util.Collections;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Map;
import java.util.Set;

import io.daos.DaosUtils;
import org.apache.hadoop.conf.Configuration;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * DAOS Filesystem configuration for listing all DAOS FS specific configs. It also gives config help.
 */
public class DaosFsConfig {

  private Set<String> fsConfigNames;

  private String configHelp;

  private Set<String> mergeExcluded = new HashSet<>();

  private static final Logger log = LoggerFactory.getLogger(DaosFsConfig.class);

  private static final DaosFsConfig _INSTANCE = new DaosFsConfig();

  private DaosFsConfig() {
    fsConfigNames = collectFsConfigNames();
    String exampleFile = "daos-config.txt";
    try (BufferedReader reader = new BufferedReader(new InputStreamReader(
        this.getClass().getResourceAsStream("/" + exampleFile)))) {
      String line;
      StringBuilder sb = new StringBuilder();
      while ((line = reader.readLine()) != null) {
        sb.append(line).append("\n");
      }
      configHelp = sb.toString();
    } catch (Exception e) {
      throw new IllegalStateException("cannot read description from " + exampleFile, e);
    }
    mergeExcluded.add(Constants.DAOS_DEFAULT_FS);
    mergeExcluded.add(Constants.DAOS_POOL_ID);
    mergeExcluded.add(Constants.DAOS_CONTAINER_ID);
    mergeExcluded.add(Constants.DAOS_SERVER_GROUP);
    mergeExcluded.add(Constants.DAOS_POOL_FLAGS);
  }

  private Set<String> collectFsConfigNames() {
    Set<String> fsNames = new HashSet<>();
    Field[] fields = Constants.class.getFields();
    for (Field field : fields) {
      try {
        Object value = field.get(null);
        if (value instanceof String) {
          String s = (String)value;
          if (s.startsWith(Constants.DAOS_CONFIG_PREFIX) && s.length() > Constants.DAOS_CONFIG_PREFIX.length()) {
            fsNames.add(s);
          }
        }
      } catch (IllegalAccessException e) {
        log.error("failed to get field value, " + field.getName());
      }
    }
    return fsNames;
  }

  public String getConfigHelp() {
    return configHelp;
  }

  public Set<String> getFsConfigNames() {
    return Collections.unmodifiableSet(fsConfigNames);
  }

  public static final DaosFsConfig getInstance() {
    return _INSTANCE;
  }

  /**
   * merge default configuration with hadoop configuration. And hadoop configuration has higher priority than default.
   *
   * @param choice
   * configuration choice
   * @param hadoopConfig
   * hadoop configuration from user, typically from {@link org.apache.hadoop.fs.FileSystem#get(URI, Configuration)}
   */
  public void merge(String choice, Configuration hadoopConfig, Map<String, String> daosAttrMap) {
    boolean hasChoice = false;
    if (!DaosUtils.isEmptyStr(choice)) {
      hasChoice = true;
      choice += ".";
    } else {
      choice = "";
    }
    Iterator<String> it = fsConfigNames.iterator();
    while (it.hasNext()) {
      String name = it.next();
      if (hadoopConfig.get(name) == null && (!mergeExcluded.contains(name))) { // not set by user
        String choiceName = choice + name;
        String value = hasChoice ? daosAttrMap.getOrDefault(choiceName, daosAttrMap.get(name)) : daosAttrMap.get(name);
        if (value != null) {
          hadoopConfig.set(name, value);
        }
      }
    }
  }
}
