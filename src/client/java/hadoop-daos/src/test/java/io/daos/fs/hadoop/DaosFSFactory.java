/**
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop;

import io.daos.dfs.DaosFile;
import io.daos.dfs.DaosFsClient;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FileSystem;

import java.io.IOException;

/**
 *
 */
public class DaosFSFactory {
  public final static String defaultPoolId = "a9cbecc6-6ee6-4bf6-9be4-58e0c73ea4cb";
  public final static String defaultContId = "30059a26-df7f-4e65-9870-705105fd67b7";
  public final static String pooluuid = System.getProperty("pool_id", defaultPoolId);
  public final static String contuuid = System.getProperty("cont_id", defaultContId);
  public static final String defaultPoolLabel = "pool1";
  public static final String defaultContLabel = "cont1";
  public final static String poolLabel = System.getProperty("pool_label", defaultPoolLabel);
  public final static String contLabel = System.getProperty("cont_label", defaultContLabel);

  public static final String DAOS_URI = "daos://" + DaosFSFactory.getPooluuid() + "/" + DaosFSFactory.getContuuid();

  private static FileSystem createFS() throws IOException {
    Configuration conf = new Configuration();
    config(conf);
    return DaosHadoopTestUtils.createTestFileSystem(conf);
  }

  public static void config(Configuration conf) {
    config(conf, false);
  }

  public static void config(Configuration conf, boolean async) {
    conf.set(Constants.DAOS_POOL_ID, pooluuid);
    conf.set(Constants.DAOS_CONTAINER_ID, contuuid);
    conf.set(Constants.DAOS_IO_ASYNC, String.valueOf(async));
    conf.setBoolean(Constants.DAOS_WITH_UNS_PREFIX, false);
  }

  public synchronized static FileSystem getFS() throws IOException {
    prepareFs();
    return createFS();
  }

  public synchronized static DaosFsClient getFsClient() throws IOException {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(pooluuid).containerId(contuuid);
    return builder.build();
  }

  private static DaosFsClient prepareFs() throws IOException {
    try {
      DaosFsClient client = getFsClient();
      //clear all content
      DaosFile daosFile = client.getFile("/");
      String[] children = daosFile.listChildren();
      for (String child : children) {
        if (child.length() == 0 || ".".equals(child)) {
          continue;
        }
        String path = "/" + child;
        DaosFile childFile = client.getFile(path);
        if (childFile.delete(true)) {
          System.out.println("deleted folder " + path);
        } else {
          System.out.println("failed to delete folder " + path);
        }
      }
      return client;
    } catch (Exception e) {
      System.out.println("failed to clear/prepare file system");
      e.printStackTrace();
    }
    return null;
  }

  public static String getPooluuid() {
    return pooluuid;
  }

  public static String getContuuid() {
    return contuuid;
  }

  public static String getPoolLabel() {
    return poolLabel;
  }

  public static String getContLabel() {
    return contLabel;
  }

}
