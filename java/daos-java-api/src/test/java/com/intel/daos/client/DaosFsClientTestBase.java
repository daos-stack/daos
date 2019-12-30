package com.intel.daos.client;

public class DaosFsClientTestBase {

  public static final String DEFAULT_POOL_ID = "0eba76a4-5f9d-4c47-91c7-545b3677fb28";
//  public static final String DEFAULT_CONT_ID = "ffffffff-ffff-ffff-ffff-ffffffffffff";
  public static final String DEFAULT_CONT_ID = "676074c6-a33a-4e07-8990-fe9279065145";

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
