package com.intel.daos.hadoop.fs;

import com.intel.daos.client.DaosFile;
import com.intel.daos.client.DaosFsClient;
import org.apache.hadoop.conf.Configuration;
import org.apache.hadoop.fs.FileSystem;

import java.io.IOException;

/**
 *
 */
public class DaosFSFactory {

//  public final static String pooluuid = "53a47469-ea2a-418e-89d3-6d1df1aaadb4";
//  public final static String contuuid = "9e60aff2-ca28-45fe-bdb0-d1a6c182c342";
  public final static String defaultPoolId = "6614df7a-a13f-4f7b-aa1e-0ee3c6804456";
  public final static String defaultContId = "fe258219-5c5f-4f48-bcd4-143996a770ce";
  public final static String pooluuid = System.getProperty("pool_id", defaultPoolId);
  public final static String contuuid = System.getProperty("cont_id", defaultContId);
  public final static String svc = "0";

  private static FileSystem createFS() throws IOException {
    Configuration conf = new Configuration();
    conf.set(Constants.DAOS_POOL_UUID, pooluuid);
    conf.set(Constants.DAOS_CONTAINER_UUID, contuuid);
    conf.set(Constants.DAOS_POOL_SVC, svc);
    return DaosUtils.createTestFileSystem(conf);
  }

  public synchronized static FileSystem getFS() throws IOException{
    prepareFs();
    return createFS();
  }

  public synchronized static DaosFsClient getFsClient() throws IOException{
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(pooluuid).containerId(contuuid);
    return builder.build();
  }

  private static DaosFsClient prepareFs()throws IOException{
    try {
      DaosFsClient client = getFsClient();
      //clear all content
      DaosFile daosFile = client.getFile("/");
      String[] children = daosFile.listChildren();
      for(String child : children) {
        if(child.length() == 0 || ".".equals(child)){
          continue;
        }
        String path = "/"+child;
        DaosFile childFile = client.getFile(path);
        if(childFile.delete(true)){
          System.out.println("deleted folder "+path);
        }else{
          System.out.println("failed to delete folder "+path);
        }
      }
      return client;
    }catch (Exception e){
      System.out.println("failed to clear/prepare file system");
      e.printStackTrace();
    }
    return null;
  }

  public static String getPoolUuid() {
    return pooluuid;
  }

  public static String getContUuid() {
    return contuuid;
  }
}
