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
  public final static String defaultPoolId = "bce96e2f-5a52-4aeb-b0b4-8994eee79a5f";
  public final static String defaultContId = "e7814443-c8a1-4014-b94b-571ac7f5cff3";
  public final static String pooluuid = System.getProperty("pool_id", defaultPoolId);
  public final static String contuuid = System.getProperty("cont_id", defaultContId);
  public final static String svc = "0";

  private static FileSystem createFS() throws IOException {
    Configuration conf = new Configuration();
    config(conf);
    return DaosHadoopTestUtils.createTestFileSystem(conf);
  }

  public static void config(Configuration conf) {
    config(conf, false);
  }

  public static void config(Configuration conf, boolean async) {
    conf.set(Constants.DAOS_POOL_UUID, pooluuid);
    conf.set(Constants.DAOS_CONTAINER_UUID, contuuid);
    conf.set(Constants.DAOS_POOL_SVC, svc);
    conf.set(Constants.DAOS_IO_ASYNC, String.valueOf(async));
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
}
