int clean_net_buf_alloc_checked_for_null(struct net_buf_pool* pool)
{
    struct net_buf* buf = net_buf_alloc(pool, K_NO_WAIT);
    if (buf == NULL)
    {
        return -ENOMEM;
    }
    net_buf_unref(buf);
    return 0;
}

int clean_net_buf_alloc_negated(struct net_buf_pool* pool)
{
    struct net_buf* buf;
    buf = net_buf_alloc(pool, K_NO_WAIT);
    if (!buf)
    {
        return -ENOMEM;
    }
    net_buf_unref(buf);
    return 0;
}

int clean_net_buf_ownership_passed_on(struct net_buf_pool* pool)
{
    struct net_buf* buf = net_buf_alloc(pool, K_NO_WAIT);
    if (buf == NULL)
    {
        return -ENOMEM;
    }
    PASS_OWNERSHIP(buf);
    return 0;
}

int clean_net_buf_reference_released(struct net_buf* buf)
{
    struct net_buf* extra = net_buf_ref(buf);
    send(extra);
    net_buf_unref(extra);
    return 0;
}

int leak_net_buf_early_return(struct net_buf_pool* pool, int flag)
{
    struct net_buf* buf = net_buf_alloc(pool, K_NO_WAIT);
    if (buf == NULL)
    {
        return -ENOMEM;
    }
    if (flag)
    {
        return 1; // LEAK: buf
    }
    net_buf_unref(buf);
    return 0;
}

int leak_net_buf_alloc_not_checked(struct net_buf_pool* pool)
{
    struct net_buf* buf = net_buf_alloc(pool, K_NO_WAIT);
    return 0; // LEAK: buf
}

int leak_net_buf_reference_never_released(struct net_buf* buf)
{
    struct net_buf* extra = net_buf_ref(buf);
    send(extra);
    return 0; // LEAK: extra
}

int leak_net_buf_reference_released_through_the_original(struct net_buf* buf)
{
    struct net_buf* extra = net_buf_ref(buf);
    net_buf_unref(buf);
    return 0; // LEAK: extra
}
