/** @releases net_buf buf */
static void consume(struct net_buf* buf);

/** @releases net_buf buf */
void clean_released_on_all_paths(struct net_buf* buf, int flag)
{
    if (flag)
    {
        net_buf_unref(buf);
        return;
    }
    net_buf_unref(buf);
}

/** @releases net_buf buf */
void clean_handed_to_a_releasing_callee(struct net_buf* buf)
{
    consume(buf);
}

/** @releases net_buf buf */
int clean_marked_after_a_queue_put(struct k_msgq* queue, struct net_buf* buf)
{
    if (k_msgq_put(queue, &buf, K_NO_WAIT) != 0)
    {
        net_buf_unref(buf);
        return -ENOBUFS;
    }
    PASS_OWNERSHIP(buf);
    return 0;
}

/** @releases net_buf buf */
void clean_null_arm_of_a_parameter_test(struct net_buf* buf)
{
    if (buf != NULL)
    {
        net_buf_unref(buf);
    }
}

/** @releases net_buf buf */
void tests_a_reassigned_parameter(struct net_buf* buf)
{
    buf = NULL;
    if (buf == NULL)
    {
        return; // LEAK: buf
    }
    net_buf_unref(buf);
}

/** @releases net_buf buf */
int skips_the_release_on_one_path(struct net_buf* buf, int flag)
{
    if (flag)
    {
        return -EINVAL; // LEAK: buf
    }
    net_buf_unref(buf);
    return 0;
}

/** @releases net_buf buf */
void releases_the_parameter_twice(struct net_buf* buf)
{
    net_buf_unref(buf);
    net_buf_unref(buf); // RELEASED TWICE: buf
}

/** @releases net_buf buf */
void named_differently_in_the_definition(struct net_buf* buf);

void named_differently_in_the_definition(struct net_buf* message)
{
    net_buf_reset(message);
} // LEAK: message
