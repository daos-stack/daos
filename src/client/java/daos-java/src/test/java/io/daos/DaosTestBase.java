package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "f6b87ce6-6ce5-478f-8631-58bdcf49b3c1";
  public static final String DEFAULT_CONT_ID = "4aa9b4d7-88d5-4e21-9942-2d1d263ff370";

  public static final String DEFAULT_OBJECT_CONT_ID = "0ab1f374-25b8-4154-9e23-efeb94829c25";

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
