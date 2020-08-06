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

package io.daos.fs.hadoop;

import java.io.InputStream;
import java.lang.reflect.Field;
import java.net.URI;
import java.util.Collections;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Map;
import java.util.Set;

import javax.xml.parsers.DocumentBuilderFactory;

import org.apache.commons.configuration.ConfigurationException;
import org.apache.commons.lang.StringUtils;
import org.apache.hadoop.conf.Configuration;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.NodeList;

/**
 * A class for reading daos-site.xml, if any, from class path as well as verifying and merging Hadoop configuration.
 *
 * <B>daos-site.xml</B>
 * Should be put under class path's root for initializing {@link DaosFileSystem}. If not present in class path, DAOS URI
 * must be default URI (daos:///). And pool UUID and container UUID must be provided in Hadoop configuration then.
 *
 * <B>configuration verification</B>
 * pool UUID and container UUID must be provided by either Hadoop configuration or daos-site.xml if any. If DAOS URI
 * is default URI and UUIDs are provided by Hadoop configuration, the UUIDs must be same as ones in daos-site.xml if
 * any.
 *
 * <B>merge configuration</B>
 * see {@link #getDaosUriDesc()} and {@link #parseConfig(String, Configuration)} for how configurations are
 * read and merged.
 */
public class DaosConfigFile {

  private Configuration defaultConfig;

  private Set<String> fsConfigNames;

  private String daosUriDesc;

  private static final Logger log = LoggerFactory.getLogger(DaosConfigFile.class);

  private static final DaosConfigFile _INSTANCE = new DaosConfigFile();

  private DaosConfigFile() {
    defaultConfig = new Configuration(false);
    defaultConfig.addResource(Constants.DAOS_CONFIG_FILE_NAME);
    if (log.isDebugEnabled()) {
      log.debug("configs from " + Constants.DAOS_CONFIG_FILE_NAME);
      Iterator<Map.Entry<String, String>> it = defaultConfig.iterator();
      while (it.hasNext()) {
        Map.Entry<String, String> item = it.next();
        if (item.getKey().startsWith("fs.daos.")) {
          log.debug(item.getKey() + "=" + item.getValue());
        }
      }
    }
    String exampleFile = "daos-site-example.xml";
    try (InputStream is = this.getClass().getResourceAsStream("/" + exampleFile)) {
      Document document = DocumentBuilderFactory.newInstance().newDocumentBuilder().parse(is);
      NodeList names = document.getElementsByTagName("name");
      Element targetNode = null;
      for (int i = 0; i < names.getLength(); i++) {
        Element node = (Element)names.item(i);
        if (Constants.DAOS_DEFAULT_FS.equals(node.getTextContent().trim())) {
          targetNode = (Element)node.getParentNode();
          break;
        }
      }
      if (targetNode == null) {
        throw new ConfigurationException("cannot find " + Constants.DAOS_DEFAULT_FS + " node from " + exampleFile);
      }
      Element desc = (Element)targetNode.getElementsByTagName("description").item(0);
      daosUriDesc = desc.getTextContent();
    } catch (Exception e) {
      throw new IllegalStateException("cannot read description from " + exampleFile, e);
    }
    fsConfigNames = collectFsConfigNames();
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

  public Set<String> getFsConfigNames() {
    return Collections.unmodifiableSet(fsConfigNames);
  }

  public static final DaosConfigFile getInstance() {
    return _INSTANCE;
  }

  /**
   * get configuration from daos-site.xml if any.
   *
   * @param name
   * name of configuration
   * @return value of configuration, null if not configured.
   */
  public String getFromDaosFile(String name) {
    return defaultConfig.get(name);
  }

  /**
   * get configuration from daos-site.xml, if any, with default values.
   *
   * @param name
   * name of configuration
   * @param value
   * default value to return
   * @return value of configuration, <code>value</code> if not configured.
   */
  public String getFromDaosFile(String name, String value) {
    return defaultConfig.get(name, value);
  }

  /**
   * verify if <code>authority</code> in URI are valid in terms of pool UUID and container UUID.
   * For other configurations, fallback on default DAOS configuration if no specific configurations for
   * <code>authority</code>.
   * Configurations from <code>hadoopConfig</code> have higher priority than ones from daos-site.xml.
   *
   * @param authority
   * A valid URI authority name to denote unique pool and container. The empty value means no authority provided in URI
   *     which is default URI. see {@link #getDaosUriDesc()} for details.
   * @param hadoopConfig
   * configuration from Hadoop, could be manipulated by upper layer application, like Spark
   * @return verified and merged configuration
   */
  Configuration parseConfig(String authority, Configuration hadoopConfig) {
    setUuid(authority, Constants.DAOS_POOL_UUID, hadoopConfig);
    setUuid(authority, Constants.DAOS_CONTAINER_UUID, hadoopConfig);
    //set other configurations after the UUIDs are set
    return merge(StringUtils.isEmpty(authority) ? "" : (authority + "."), hadoopConfig, null);
  }

  private void setUuid(String authority, String configName, Configuration hadoopConfig) {
    String hid = hadoopConfig.get(configName);
    if (StringUtils.isEmpty(authority)) { // default URI
      if (StringUtils.isEmpty(hid)) { // make sure UUID is set
        String did = defaultConfig.get(configName);
        if (StringUtils.isEmpty(did)) {
          throw new IllegalArgumentException(configName + " is neither specified nor default value found.");
        }
        hadoopConfig.set(configName, did);
      }
      return;
    }
    // non-default
    if (StringUtils.isEmpty(hid)) { // make sure UUID is set
      String did = defaultConfig.get(authority + "." + configName) ;
      if (StringUtils.isEmpty(did)) {
        throw new IllegalArgumentException(configName + " is neither specified nor default value found for authority " +
                authority);
      }
      hadoopConfig.set(configName, did);
    }
  }

  /**
   * merge default configuration with hadoop configuration. And hadoop configuration has higher priority than default.
   *
   * @param authority
   * URI authority
   * @param hadoopConfig
   * hadoop configuration from user, typically from {@link org.apache.hadoop.fs.FileSystem#get(URI, Configuration)}
   * @param excludeProps
   * excluded properties from merging
   * @return merged hadoop configuration
   */
  public Configuration merge(String authority, Configuration hadoopConfig, Set<String> excludeProps) {
    Iterator<String> it = fsConfigNames.iterator();
    while (it.hasNext()) {
      String name = it.next();
      if (excludeProps == null || !excludeProps.contains(name)) {
        if (hadoopConfig.get(name) == null) { //not set by user
          String value = StringUtils.isEmpty(authority) ? defaultConfig.get(name) :
              defaultConfig.get(authority + name, defaultConfig.get(name));
          if (value != null) {
            hadoopConfig.set(name, value);
          }
        }
      }
    }
    return hadoopConfig;
  }

  /**
   * get DAOS URI description of,
   * - how DAOS URI is constructed
   * - how pool UUID and container UUID are mapped
   * - how config values are read.
   *
   * @return DAOS URI description
   */
  public String getDaosUriDesc() {
    return daosUriDesc;
  }
}
