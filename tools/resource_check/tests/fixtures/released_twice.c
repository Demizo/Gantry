int twice_on_one_branch(struct net_buf_pool* pool, int flag)
{
    struct net_buf* buf = net_buf_alloc(pool, K_NO_WAIT);
    if (buf == NULL)
    {
        return -ENOMEM;
    }
    net_buf_unref(buf);
    if (flag)
    {
        net_buf_unref(buf); // RELEASED TWICE: buf
    }
    return 0;
}

int clean_once_on_each_exclusive_branch(struct net_buf_pool* pool, int flag)
{
    struct net_buf* buf = net_buf_alloc(pool, K_NO_WAIT);
    if (buf == NULL)
    {
        return -ENOMEM;
    }
    if (flag)
    {
        net_buf_unref(buf);
    }
    else
    {
        net_buf_unref(buf);
    }
    return 0;
}

int clean_reacquired_between_releases(void)
{
    void* block = NULL;
    if (MEM_ALLOC(4, &block) != SUCCESS)
    {
        return -ENOMEM;
    }
    MEM_UNREF(&block);
    if (MEM_ALLOC(8, &block) != SUCCESS)
    {
        return -ENOMEM;
    }
    MEM_UNREF(&block);
    return 0;
}

void clean_consumes_a_parameter(struct net_buf* buf)
{
    net_buf_unref(buf);
}

void consumes_a_parameter_twice(struct net_buf* buf)
{
    net_buf_unref(buf);
    net_buf_unref(buf); // RELEASED TWICE: buf
}

int clean_passed_on_reacquired_and_passed_on_again(void)
{
    void* block = NULL;
    if (MEM_ALLOC(4, &block) != SUCCESS)
    {
        return -ENOMEM;
    }
    PASS_OWNERSHIP(block);
    if (MEM_ALLOC(4, &block) != SUCCESS)
    {
        return -ENOMEM;
    }
    PASS_OWNERSHIP(block);
    MEM_REF(block);
    PASS_OWNERSHIP(block);
    return 0;
}

int clean_passed_on_in_a_loop(int count)
{
    void* block = NULL;
    for (int i = 0; i < count; i++)
    {
        if (MEM_ALLOC(4, &block) != SUCCESS)
        {
            return -ENOMEM;
        }
        PASS_OWNERSHIP(block);
    }
    return 0;
}

int passed_on_after_release(void)
{
    void* block = NULL;
    if (MEM_ALLOC(4, &block) != SUCCESS)
    {
        return -ENOMEM;
    }
    MEM_UNREF(&block);
    PASS_OWNERSHIP(block); // RELEASED TWICE: block
    return 0;
}

int second_reference_in_the_same_identifier(void* block)
{
    MEM_REF(block);
    MEM_REF(block);
    MEM_UNREF(&block);
    MEM_UNREF(&block); // RELEASED TWICE: block
    return 0;
}

int clean_second_reference_in_its_own_identifier(void* block)
{
    void* extra = block;
    MEM_REF(block);
    MEM_REF(extra);
    MEM_UNREF(&block);
    MEM_UNREF(&extra);
    return 0;
}

int clean_release_in_a_loop_after_acquire_in_the_same_iteration(struct net_buf_pool* pool, int count)
{
    for (int i = 0; i < count; i++)
    {
        struct net_buf* buf = net_buf_alloc(pool, K_NO_WAIT);
        if (buf == NULL)
        {
            return -ENOMEM;
        }
        net_buf_unref(buf);
    }
    return 0;
}

int twice_after_goto(struct net_buf_pool* pool, int flag)
{
    struct net_buf* buf = net_buf_alloc(pool, K_NO_WAIT);
    if (buf == NULL)
    {
        return -ENOMEM;
    }
    if (flag)
    {
        net_buf_unref(buf);
        goto out;
    }
    net_buf_unref(buf);
out:
    net_buf_unref(buf); // RELEASED TWICE: buf
    return 0;
}
