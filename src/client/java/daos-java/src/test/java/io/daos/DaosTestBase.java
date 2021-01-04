package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "64448ced-ee95-4871-abf7-f07aa59ab779";
  public static final String DEFAULT_CONT_ID = "6c4682f0-6d5c-448a-845f-7f534ea2c52b";

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
