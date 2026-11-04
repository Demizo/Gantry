struct holder
{
    void* block;
};

/** @acquires memory out on-success */
int clean_stored_through_the_target(void** out)
{
    void* block = NULL;
    if (MEM_ALLOC(4, &block) != SUCCESS)
    {
        return -ENOMEM;
    }
    *out = block;
    return SUCCESS;
}

/** @acquires memory holder on-success */
int clean_stored_in_a_field_of_the_target(struct holder* holder)
{
    void* block = NULL;
    if (MEM_ALLOC(4, &block) != SUCCESS)
    {
        return -ENOMEM;
    }
    holder->block = block;
    return SUCCESS;
}

/** @acquires memory out */
int clean_acquired_into_the_target(void* out)
{
    MEM_REF(out);
    return 0;
}

/** @acquires net_buf return on-non-null */
struct net_buf* clean_returned(struct net_buf_pool* pool)
{
    struct net_buf* buf = net_buf_alloc(pool, K_NO_WAIT);
    if (buf == NULL)
    {
        return NULL;
    }
    net_buf_add(buf, 4);
    return buf;
}

/** @acquires net_buf return on-non-null */
struct net_buf* clean_returned_acquire(struct net_buf_pool* pool)
{
    return net_buf_alloc(pool, K_NO_WAIT);
}

/** @acquires memory out on-success */
int clean_read_after_the_hand_off(void** out)
{
    void* block = NULL;
    if (MEM_ALLOC(4, &block) != SUCCESS)
    {
        return -ENOMEM;
    }
    *out = block;
    use(block);
    return SUCCESS;
}

/** @acquires memory out on-success */
int clean_released_before_a_failure_return(void** out, int flag)
{
    if (MEM_ALLOC(4, out) != SUCCESS)
    {
        return -ENOMEM;
    }
    if (flag)
    {
        MEM_UNREF(out);
        return -EIO;
    }
    return SUCCESS;
}

/** @acquires memory out on-success */
int clean_failure_arm_returns_early(void** out)
{
    if (MEM_ALLOC(4, out) != SUCCESS)
    {
        return -ENOMEM;
    }
    return SUCCESS;
}

/** @acquires memory out on-success */
int clean_result_passed_through_after_the_hand_off(void** out)
{
    int ret = MEM_ALLOC(4, out);
    LOG_DBG("allocated");
    return ret;
}

/** @acquires memory out on-success */
int hand_off_on_one_branch(void** out, int flag)
{
    void* block = NULL;
    if (MEM_ALLOC(4, &block) != SUCCESS)
    {
        return -ENOMEM;
    }
    if (flag)
    {
        *out = block;
    }
    return SUCCESS; // LEAK: block
}

/** @acquires memory out on-success */
int hand_off_of_a_different_class(struct net_buf_pool* pool, struct net_buf** out)
{
    struct net_buf* buf = net_buf_alloc(pool, K_NO_WAIT);
    if (buf == NULL)
    {
        return -ENOMEM;
    }
    *out = buf;
    return SUCCESS; // LEAK: buf
}

/** @acquires net_buf return on-non-null */
struct net_buf* returns_another_identifier(struct net_buf_pool* pool, struct net_buf* other)
{
    struct net_buf* buf = net_buf_alloc(pool, K_NO_WAIT);
    if (buf == NULL)
    {
        return NULL;
    }
    return other; // LEAK: buf
}

/** @acquires memory return on-non-null */
void* returned_acquire_of_a_different_class(struct net_buf_pool* pool)
{
    return net_buf_alloc(pool, K_NO_WAIT); // POLICY ERROR: net_buf_alloc
}

/** @acquires net_buf return */
struct net_buf* returned_acquire_under_a_different_condition(struct net_buf_pool* pool)
{
    return net_buf_alloc(pool, K_NO_WAIT); // POLICY ERROR: net_buf_alloc
}

struct net_buf* returned_acquire_without_an_own_tag(struct net_buf_pool* pool)
{
    return net_buf_alloc(pool, K_NO_WAIT); // POLICY ERROR: net_buf_alloc
}

/** @acquires net_buf return on-non-null */
struct net_buf* discarded_acquire_under_the_own_return_tag(struct net_buf_pool* pool)
{
    net_buf_alloc(pool, K_NO_WAIT); // POLICY ERROR: net_buf_alloc
    return NULL;
}

/** @acquires memory out on-success */
int released_after_the_hand_off(void** out)
{
    void* block = NULL;
    if (MEM_ALLOC(4, &block) != SUCCESS)
    {
        return -ENOMEM;
    }
    *out = block;
    MEM_UNREF(&block); // RELEASED TWICE: block
    return SUCCESS;
}

/** @acquires memory out on-success */
int released_twice_after_the_hand_off_before_a_failure_return(void** out, int flag)
{
    void* block = NULL;
    if (MEM_ALLOC(4, &block) != SUCCESS)
    {
        return -ENOMEM;
    }
    *out = block;
    if (flag)
    {
        MEM_UNREF(&block);
        MEM_UNREF(&block); // RELEASED TWICE: block
        return -EIO;
    }
    return SUCCESS;
}

/** @acquires memory out */
void released_after_acquiring_into_the_target(void* out)
{
    MEM_REF(out);
    MEM_UNREF(&out); // RELEASED TWICE: out
}

/** @acquires memory out on-success */
int released_on_a_path_that_may_succeed(void** out, int flag)
{
    if (MEM_ALLOC(4, out) != SUCCESS)
    {
        return -ENOMEM;
    }
    MEM_UNREF(out); // RELEASED TWICE: out
    if (flag)
    {
        return -EIO;
    }
    return SUCCESS;
}

/** @acquires memory out on-success */
int released_twice_before_a_failure_return(void** out, int flag)
{
    if (MEM_ALLOC(4, out) != SUCCESS)
    {
        return -ENOMEM;
    }
    if (flag)
    {
        MEM_UNREF(out);
        MEM_UNREF(out); // RELEASED TWICE: out
        return -EIO;
    }
    return SUCCESS;
}

/** @acquires memory out on-success */
int hand_off_then_failure(void** out, int flag)
{
    void* block = NULL;
    if (MEM_ALLOC(4, &block) != SUCCESS)
    {
        return -ENOMEM;
    }
    *out = block;
    if (flag)
    {
        return -EIO; // LEAK: block
    }
    return SUCCESS;
}

/** @acquires memory out on-success */
int hand_off_then_negative_number(void** out, int flag)
{
    void* block = NULL;
    if (MEM_ALLOC(4, &block) != SUCCESS)
    {
        return -ENOMEM;
    }
    *out = block;
    if (flag)
    {
        return -1; // LEAK: block
    }
    return SUCCESS;
}

/** @acquires memory out on-non-null */
void* hand_off_then_null(void** out, int flag)
{
    void* block = NULL;
    if (MEM_ALLOC(4, &block) != SUCCESS)
    {
        return NULL;
    }
    *out = block;
    if (flag)
    {
        return NULL; // LEAK: block
    }
    return *out;
}
