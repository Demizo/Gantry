int clean_mem_slab_alloc_checked(void)
{
    void* block = NULL;
    if (k_mem_slab_alloc(&g_slab, &block, K_NO_WAIT) != 0)
    {
        return -ENOMEM;
    }
    k_mem_slab_free(&g_slab, block);
    return 0;
}

int clean_mem_slab_alloc_result_variable(void)
{
    void* block = NULL;
    int ret = k_mem_slab_alloc(&g_slab, &block, K_FOREVER);
    if (ret != SUCCESS)
    {
        return ret;
    }
    k_mem_slab_free(&g_slab, block);
    return 0;
}

int leak_mem_slab_early_return(int flag)
{
    void* block = NULL;
    if (k_mem_slab_alloc(&g_slab, &block, K_NO_WAIT) != 0)
    {
        return -ENOMEM;
    }
    if (flag)
    {
        return 1; // LEAK: block
    }
    k_mem_slab_free(&g_slab, block);
    return 0;
}

int leak_mem_slab_alloc_not_checked(void)
{
    void* block = NULL;
    k_mem_slab_alloc(&g_slab, &block, K_NO_WAIT);
    return 0; // LEAK: block
}

int leak_mem_slab_block_freed_to_the_heap(void)
{
    void* block = NULL;
    if (k_mem_slab_alloc(&g_slab, &block, K_NO_WAIT) != 0)
    {
        return -ENOMEM;
    }
    k_free(block);
    return 0; // LEAK: block
}
