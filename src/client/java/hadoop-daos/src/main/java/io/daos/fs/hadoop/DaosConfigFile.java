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

import java.io.*;
import java.util.Iterator;
import java.util.Map;

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
 * Should be put under class path's root. If not present in class path, DAOS URI must be default URI (daos://default:0).
 * Pool UUID and container UUID must be provided in Hadoop configuration.
 *
 * <B>configuration verification</B>
 * pool UUID and container UUID must be provided by either Hadoop configuration or daos-site.xml if any. If DAOS URI
 * is default URI and UUIDs are provided by Hadoop configuration, the UUIDs must be same as ones in daos-site.xml if
 * any.
 *
 * <B>merge configuration</B>
 * see {@link #getDaosUriDesc()} and {@link #parseConfig(String, String, Configuration)} for how configurations are
 * read and merged.
 */
public class DaosConfigFile {

  private Configuration defaultConfig;

  private String daosUriDesc;

  private static final Logger log = LoggerFactory.getLogger(DaosConfigFile.class);

  private static final DaosConfigFile _INSTANCE = new DaosConfigFile();

  private DaosConfigFile() {
    defaultConfig = new Configuration(false);
    defaultConfig.addResource("daos-site.xml");
    if (log.isDebugEnabled()) {
      log.debug("configs from daos-site.xml");
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
      for (int i=0; i<names.getLength(); i++) {
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
  }

  public static final DaosConfigFile getInstance() {
    return _INSTANCE;
  }

  /**
   * get configuration from daos-site.xml if any.
   * @param name
   * name of configuration
   * @return value of configuration, null if not configured.
   */
  public String getFromDaosFile(String name) {
    return defaultConfig.get(name);
  }

  /**
   * get configuration from daos-site.xml, if any, with default values.
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
   * verify if pkey and ckey in URI are valid in terms of pool UUID and container UUID.
   * For other configurations, fallback on default DAOS configuration if no specific configurations for pkey+c+ckey.
   * Configurations from <code>hadoopConfig</code> have higher priority than ones from daos-site.xml.
   * @param pkey
   * pool key mapped to real pool UUID configured in daos-site.xml if any. see {@link #getDaosUriDesc()} for details.
   * @param ckey
   * container key mapped to real container UUID configured in daos-site.xml if any. see {@link #getDaosUriDesc()}
   * for details.
   * @param hadoopConfig
   * configuration from Hadoop, could be manipulated by upper layer application, like Spark
   * @return verified and merged configuration
   */
  Configuration parseConfig(String pkey, String ckey, Configuration hadoopConfig) {
    StringBuilder sb = new StringBuilder();
    pkey = setUuid(pkey, Constants.DAOS_CONFIG_POOL_KEY_DEFAULT, Constants.DAOS_POOL_UUID, hadoopConfig);
    sb.append(pkey);

    ckey = setUuid(ckey, Constants.DAOS_CONFIG_CONTAINER_KEY_DEFAULT, Constants.DAOS_CONTAINER_UUID, hadoopConfig);
    if (!ckey.isEmpty()) {
      sb.append(Constants.DAOS_CONFIG_CONTAINER_KEY_PREFIX).append(ckey);
    }
    if (sb.length() > 0) {
      sb.append('.');
    }
    //set other configurations after the UUIDs are set
    return merge(sb.toString(), hadoopConfig);
  }

  private String setUuid(String key, String defaultKey, String configName, Configuration hadoopConfig) {
    String hid = hadoopConfig.get(configName);
    if (defaultKey.equals(key)) {
      String did = defaultConfig.get(configName);
      if (!StringUtils.isEmpty(hid) && !StringUtils.isEmpty(did) && !hid.equals(did)) {
        throw new IllegalArgumentException("Inconsistent value of " + configName + ", from hadoop: " + hid +
                ". from " + Constants.DAOS_CONFIG_FILE_NAME + ": " + did +
                ".\n Considering to change your URI to non-default. See daos URI description. \n" +
                daosUriDesc);
      }
      if (StringUtils.isEmpty(hid)) { //make sure UUID is set
        if (StringUtils.isEmpty(did)) {
          throw new IllegalArgumentException(configName + " is neither specified nor default value found.");
        }
        hadoopConfig.set(configName, did);
      }
      return "";
    }
    // non-default
    if (StringUtils.isEmpty(hid)) { //make sure UUID is set
      String prefix = Constants.DAOS_POOL_UUID.equals(configName) ?
              key + "." : Constants.DAOS_CONFIG_CONTAINER_KEY_PREFIX + key + ".";
      String did = defaultConfig.get(prefix + configName) ;
      if (StringUtils.isEmpty(did)) {
        throw new IllegalArgumentException(configName + " is neither specified nor default value found for key " +
                prefix);
      }
      hadoopConfig.set(configName, did);
    }
    return key;
  }

  private Configuration merge(String prefix, Configuration hadoopConfig) {
    Iterator<Map.Entry<String, String>> it = defaultConfig.iterator();
    while (it.hasNext()) {
      Map.Entry<String, String> item = it.next();
      String name = item.getKey();
      if (name.startsWith("fs.daos.")) {
        if (hadoopConfig.get(name) == null) { //not set by user
          hadoopConfig.set(name, defaultConfig.get(prefix+name, item.getValue()));
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
   * @return DAOS URI description
   */
  public String getDaosUriDesc() {
    return daosUriDesc;
  }
}
