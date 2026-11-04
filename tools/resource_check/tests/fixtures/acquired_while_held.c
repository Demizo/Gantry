void out_parameter_acquired_twice(void)
{
    void* block = NULL;
    MEM_ALLOC(4, &block);
    MEM_ALLOC(4, &block); // ACQUIRED WHILE HELD: block
    MEM_UNREF(&block);
}

void assigned_twice(struct net_buf_pool* pool)
{
    struct net_buf* buf = net_buf_alloc(pool, K_NO_WAIT);
    buf = net_buf_alloc(pool, K_NO_WAIT); // ACQUIRED WHILE HELD: buf
    net_buf_unref(buf);
}

void acquired_in_a_loop_without_a_release(int count)
{
    void* block = NULL;
    for (int i = 0; i < count; i++)
    {
        MEM_ALLOC(4, &block); // ACQUIRED WHILE HELD: block
    }
    MEM_UNREF(&block);
}

void referenced_while_held(struct net_buf_pool* pool)
{
    struct net_buf* buf = net_buf_alloc(pool, K_NO_WAIT);
    buf = net_buf_ref(buf); // ACQUIRED WHILE HELD: buf
    net_buf_unref(buf);
}

/** @releases net_buf buf */
void acquired_over_a_parameter_promised_to_be_released(struct net_buf_pool* pool, struct net_buf* buf)
{
    buf = net_buf_alloc(pool, K_NO_WAIT); // ACQUIRED WHILE HELD: buf
    net_buf_unref(buf);
}

void declared_in_each_iteration_of_a_leaky_loop(struct net_buf_pool* pool, int count)
{
    for (int i = 0; i < count; i++)
    {
        struct net_buf* buf = net_buf_alloc(pool, K_NO_WAIT);
        stash(buf);
    }
} // LEAK: buf

/** @acquires memory out */
void clean_acquired_again_after_a_hand_off(void** out)
{
    void* block = NULL;
    MEM_ALLOC(4, &block);
    *out = block;
    MEM_ALLOC(4, &block);
    MEM_UNREF(&block);
}

void clean_retried_on_the_failure_arm_after_an_assignment(void)
{
    void* block = NULL;
    MEM_ALLOC(4, &block);
    stash(block);
    block = NULL;
    if (MEM_ALLOC(4, &block) != SUCCESS)
    {
        MEM_ALLOC(8, &block);
    }
    MEM_UNREF(&block);
}

void clean_referenced_borrowed_parameter(struct net_buf* buf)
{
    buf = net_buf_ref(buf);
    net_buf_unref(buf);
}

void clean_released_in_between(void)
{
    void* block = NULL;
    MEM_ALLOC(4, &block);
    MEM_UNREF(&block);
    MEM_ALLOC(4, &block);
    MEM_UNREF(&block);
}

void clean_passed_on_in_between(void** kept)
{
    void* block = NULL;
    MEM_ALLOC(4, &block);
    *kept = block;
    PASS_OWNERSHIP(block);
    MEM_ALLOC(4, &block);
    MEM_UNREF(&block);
}

void clean_retried_on_the_failure_arm(struct net_buf_pool* pool)
{
    struct net_buf* buf = net_buf_alloc(pool, K_NO_WAIT);
    if (buf == NULL)
    {
        buf = net_buf_alloc(pool, K_FOREVER);
    }
    net_buf_unref(buf);
}
