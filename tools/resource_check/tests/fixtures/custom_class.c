/** @acquires lease handle */
static int lease_take(int id, int** handle);

/** @releases lease handle */
static void lease_drop(int** handle);

/** @acquires lease handle */
#define LEASE_TAKE(id, handle) lease_take(id, handle)

/** @releases lease handle */
#define LEASE_DROP(handle) lease_drop(handle)

int clean_function_tags(void)
{
    int* lease;
    lease_take(1, &lease);
    lease_drop(&lease);
    return 0;
}

int leak_function_tags(int flag)
{
    int* lease;
    lease_take(1, &lease);
    if (flag)
    {
        return 1; // LEAK: lease
    }
    lease_drop(&lease);
    return 0;
}

int clean_macro_tags(void)
{
    int* lease;
    LEASE_TAKE(1, &lease);
    LEASE_DROP(&lease);
    return 0;
}

int leak_macro_tags(int flag)
{
    int* lease;
    LEASE_TAKE(1, &lease);
    if (flag)
    {
        return 1; // LEAK: lease
    }
    LEASE_DROP(&lease);
    return 0;
}

int leak_other_class_does_not_release(void)
{
    int* lease;
    LEASE_TAKE(1, &lease);
    MEM_UNREF(&lease);
    return 0; // LEAK: lease
}

int clean_pass_ownership_releases_custom_class(void)
{
    int* lease;
    LEASE_TAKE(1, &lease);
    PASS_OWNERSHIP(lease);
    return 0;
}
