/** @acquires heap return on-non-null */
static void* heap_get(int size);

/** @releases heap block */
static void heap_put(void* block);

int clean_assigned_then_tested_for_null(void)
{
    void* block = heap_get(4);
    if (block == NULL)
    {
        return -1;
    }
    heap_put(block);
    return 0;
}

int clean_bare_pointer(void)
{
    void* block;
    block = heap_get(4);
    if (!block)
    {
        return -1;
    }
    heap_put(block);
    return 0;
}

int clean_not_equal_null(void)
{
    void* block = heap_get(4);
    if (block != NULL)
    {
        heap_put(block);
        return 0;
    }
    return -1;
}

int clean_bare_pointer_success_arm(void)
{
    void* block = heap_get(4);
    if (block)
    {
        heap_put(block);
    }
    return 0;
}

int clean_compared_with_zero(void)
{
    void* block = heap_get(4);
    if (block == 0)
    {
        return -1;
    }
    heap_put(block);
    return 0;
}

int clean_assignment_in_condition(void)
{
    void* block;
    if ((block = heap_get(4)) == NULL)
    {
        return -1;
    }
    heap_put(block);
    return 0;
}

int clean_negated_assignment_in_condition(void)
{
    void* block;
    if (!(block = heap_get(4)))
    {
        return -1;
    }
    heap_put(block);
    return 0;
}

int leak_after_non_null(int flag)
{
    void* block = heap_get(4);
    if (block == NULL)
    {
        return -1;
    }
    if (flag)
    {
        return 1; // LEAK: block
    }
    heap_put(block);
    return 0;
}

int leak_success_arm_of_pointer_test(void)
{
    void* block = heap_get(4);
    if (block != NULL)
    {
        return 0; // LEAK: block
    }
    return -1;
}

int leak_success_constant_is_not_a_null_check(void)
{
    void* block = heap_get(4);
    if (block == SUCCESS)
    {
        return -1; // LEAK: block
    }
    heap_put(block);
    return 0;
}

int leak_negative_test_is_not_a_null_check(void)
{
    void* block = heap_get(4);
    if (block < 0)
    {
        return -1; // LEAK: block
    }
    heap_put(block);
    return 0;
}
