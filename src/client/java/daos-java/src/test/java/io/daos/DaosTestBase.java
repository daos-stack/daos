package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "b7979ca9-3e1f-485b-86a1-08830a872d51";
  public static final String DEFAULT_CONT_ID = "770cf65f-0e73-4d5a-bf7b-bf1c65c6d817";

  public static final String DEFAULT_OBJECT_CONT_ID = "5c32dc71-b2a8-49b6-a339-0750e9bf0bb4";

  public static String getPoolId() {
    return System.getProperty("pool_id", DaosTestBase.DEFAULT_POOL_ID);
  }

  public static String getContId() {
    return System.getProperty("cont_id", DaosTestBase.DEFAULT_CONT_ID);
  }

  public static String getObjectContId() {
    return System.getProperty("object_cont_id", DaosTestBase.DEFAULT_OBJECT_CONT_ID);
  }
}
