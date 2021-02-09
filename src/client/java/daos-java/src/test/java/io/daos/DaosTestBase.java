package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "aab99b21-5fba-402d-9ac0-59ce9f34f998";
  public static final String DEFAULT_CONT_ID = "70941ff5-44f3-4326-a5ec-b5b237df2f6f";

  public static final String DEFAULT_OBJECT_CONT_ID = "0216e9d9-b4f8-4523-9482-8e37573f6bb9";

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
