int clean_k_malloc_checked_for_null(void)
{
    void* block = k_malloc(32);
    if (block == NULL)
    {
        return -ENOMEM;
    }
    k_free(block);
    return 0;
}

int clean_k_malloc_assignment_in_condition(void)
{
    void* block;
    if ((block = k_malloc(32)) == NULL)
    {
        return -ENOMEM;
    }
    k_free(block);
    return 0;
}

int leak_k_malloc_early_return(int flag)
{
    void* block = k_malloc(32);
    if (!block)
    {
        return -ENOMEM;
    }
    if (flag)
    {
        return 1; // LEAK: block
    }
    k_free(block);
    return 0;
}

int leak_k_malloc_never_freed(void)
{
    void* block = k_malloc(32);
    return 0; // LEAK: block
}

int leak_gantry_memory_does_not_free_a_heap_block(void)
{
    void* block = k_malloc(32);
    if (block == NULL)
    {
        return -ENOMEM;
    }
    MEM_UNREF(&block);
    return 0; // LEAK: block
}
