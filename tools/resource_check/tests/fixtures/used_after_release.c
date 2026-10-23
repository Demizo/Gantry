int passed_by_name_after_release(struct net_buf* buf)
{
    net_buf_unref(buf);
    send(buf); // USED AFTER RELEASE: buf
    return 0;
}

int passed_by_address_after_release(void)
{
    void* block = NULL;
    if (MEM_ALLOC(4, &block) != SUCCESS)
    {
        return -ENOMEM;
    }
    MEM_UNREF(&block);
    fill(&block); // USED AFTER RELEASE: block
    return 0;
}

int passed_through_a_cast_after_release(struct net_buf* buf)
{
    net_buf_unref(buf);
    log_pointer((void*)buf); // USED AFTER RELEASE: buf
    return 0;
}

int clean_assigned_after_release(struct net_buf_pool* pool, struct net_buf* buf)
{
    net_buf_unref(buf);
    buf = next_buf(pool);
    send(buf);
    return 0;
}

int clean_used_before_release(struct net_buf* buf)
{
    send(buf);
    net_buf_unref(buf);
    return 0;
}

int clean_released_on_one_branch_only(struct net_buf* buf, int flag)
{
    if (flag)
    {
        net_buf_unref(buf);
        return 0;
    }
    send(buf);
    net_buf_unref(buf);
    return 0;
}

int passed_to_a_release_of_another_class(void)
{
    void* block = NULL;
    if (MEM_ALLOC(4, &block) != SUCCESS)
    {
        return -ENOMEM;
    }
    MEM_UNREF(&block);
    net_buf_unref(block); // USED AFTER RELEASE: block
    return 0;
}

int clean_reads_other_than_call_arguments(struct net_buf* buf)
{
    net_buf_unref(buf);
    return buf == NULL;
}

int clean_member_of_released_identifier(struct net_buf* buf)
{
    net_buf_unref(buf);
    log_len(buf->len);
    return 0;
}
