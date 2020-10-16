package io.daos;

public class DaosTestBase {
  public static final String DEFAULT_POOL_ID = "a038b761-cd81-41ad-be2c-773d325448a3";
  public static final String DEFAULT_CONT_ID = "99947530-719e-42e0-bc2c-9ca3119481c9";

  public static final String DEFAULT_OBJECT_CONT_ID = "99947530-719e-42e0-bc2c-9ca3119481c9";

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
