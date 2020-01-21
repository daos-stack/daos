package com.intel.daos.client;

public class DaosFsClientTestBase {

  public static final String DEFAULT_POOL_ID = "6614df7a-a13f-4f7b-aa1e-0ee3c6804456";
//  public static final String DEFAULT_CONT_ID = "ffffffff-ffff-ffff-ffff-ffffffffffff";
  public static final String DEFAULT_CONT_ID = "fe258219-5c5f-4f48-bcd4-143996a770ce";

  public static DaosFsClient prepareFs(String poolId, String contId) throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = builder.build();

    try {
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
        childFile.release();
      }
      daosFile.release();
      return client;
    }catch (Exception e){
      System.out.println("failed to clear/prepare file system");
      e.printStackTrace();
    }
    return null;
  }

  public static void main(String args[])throws Exception{
    DaosFsClient client = null;
    try{
      client = prepareFs(DEFAULT_POOL_ID, DEFAULT_CONT_ID);
    }finally {
      client.disconnect();
    }
    if(client != null){
      System.out.println("quitting");
    }

  }
}
