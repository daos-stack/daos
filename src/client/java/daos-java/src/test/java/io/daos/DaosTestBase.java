package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "9579c9e6-7719-4720-ad69-1926f33c5b10";
  public static final String DEFAULT_CONT_ID = "e238672c-e279-4491-b5bb-0ea4fa3bf764";

  public static final String DEFAULT_OBJECT_CONT_ID = "e238672c-e279-4491-b5bb-0ea4fa3bf764";

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
