package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "65d72b12-371e-4ade-a4c0-d7627472e95b";
  public static final String DEFAULT_CONT_ID = "20aeccc9-7bac-4477-8ec0-2e090a1891d4";

  public static final String DEFAULT_OBJECT_CONT_ID = "0373b639-14d7-4a6c-b693-1592dd130467";

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
