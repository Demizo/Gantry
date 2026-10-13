int clean_failure_return_after_call_in_condition(void)
{
    void* buf = NULL;
    if (MEM_ALLOC(16, &buf) != 0)
    {
        return -ENOMEM;
    }
    MEM_UNREF(&buf);
    return 0;
}

int clean_not(void)
{
    void* buf = NULL;
    if (!MEM_ALLOC(4, &buf))
    {
        MEM_UNREF(&buf);
    }
    return 0;
}

int clean_bare(void)
{
    void* buf = NULL;
    if (MEM_ALLOC(4, &buf))
    {
        return -ENOMEM;
    }
    MEM_UNREF(&buf);
    return 0;
}

int clean_eq_zero(void)
{
    void* buf = NULL;
    if (MEM_ALLOC(4, &buf) == 0)
    {
        MEM_UNREF(&buf);
        return 0;
    }
    return -1;
}

int clean_eq_success(void)
{
    void* buf = NULL;
    if (MEM_ALLOC(4, &buf) == SUCCESS)
    {
        MEM_UNREF(&buf);
        return 0;
    }
    return -1;
}

int clean_ne_success(void)
{
    void* buf = NULL;
    if (MEM_ALLOC(4, &buf) != SUCCESS)
    {
        return -1;
    }
    MEM_UNREF(&buf);
    return 0;
}

int clean_less_than_zero(void)
{
    void* buf = NULL;
    if (MEM_ALLOC(4, &buf) < 0)
    {
        return -1;
    }
    MEM_UNREF(&buf);
    return 0;
}

int clean_greater_equal_zero(void)
{
    void* buf = NULL;
    if (MEM_ALLOC(4, &buf) >= 0)
    {
        MEM_UNREF(&buf);
        return 0;
    }
    return -1;
}

int clean_parenthesised_and_negated(void)
{
    void* buf = NULL;
    if (!((MEM_ALLOC(4, &buf)) != 0))
    {
        MEM_UNREF(&buf);
        return 0;
    }
    return -1;
}

int clean_assignment_in_condition(void)
{
    void* buf = NULL;
    int ret;
    if ((ret = MEM_ALLOC(4, &buf)) < 0)
    {
        return ret;
    }
    MEM_UNREF(&buf);
    return 0;
}

int clean_result_variable(void)
{
    void* buf = NULL;
    int ret = MEM_ALLOC(4, &buf);
    LOG_DBG("allocated");
    if (ret != SUCCESS)
    {
        return ret;
    }
    MEM_UNREF(&buf);
    return 0;
}

int clean_result_variable_assigned_separately(void)
{
    void* buf = NULL;
    int ret;
    ret = MEM_ALLOC(4, &buf);
    if (ret)
    {
        return ret;
    }
    MEM_UNREF(&buf);
    return 0;
}

int clean_failure_arm_is_the_else_arm(void)
{
    void* buf = NULL;
    if (MEM_ALLOC(4, &buf) == 0)
    {
        MEM_UNREF(&buf);
    }
    else
    {
        return -1;
    }
    return 0;
}

int clean_failure_arm_is_the_then_arm_with_else(void)
{
    void* buf = NULL;
    if (MEM_ALLOC(4, &buf))
    {
        return -1;
    }
    else
    {
        MEM_UNREF(&buf);
    }
    return 0;
}

int clean_else_if_chain(int flag)
{
    void* buf = NULL;
    if (flag)
    {
        return 1;
    }
    else if (MEM_ALLOC(4, &buf) != 0)
    {
        return -1;
    }
    MEM_UNREF(&buf);
    return 0;
}

int clean_failure_goto_cleanup(void)
{
    void* buf = NULL;
    int ret = -1;
    if (MEM_ALLOC(4, &buf) != 0)
    {
        goto out;
    }
    MEM_UNREF(&buf);
    ret = 0;
out:
    return ret;
}

int clean_conditional_acquire_in_loop(int count)
{
    void* buf = NULL;
    while (count--)
    {
        if (MEM_ALLOC(8, &buf) != 0)
        {
            return -1;
        }
        MEM_UNREF(&buf);
    }
    return 0;
}

int clean_nested_conditions(int flag)
{
    void* buf = NULL;
    if (flag)
    {
        if (MEM_ALLOC(8, &buf) != 0)
        {
            return -1;
        }
        MEM_UNREF(&buf);
    }
    return 0;
}

int clean_event_alloc_and_stow_get(enum stow_item_id id)
{
    event_t* event = NULL;
    data_value_t value;
    if (EVENT_ALLOC(&some_event, 4, &event) != SUCCESS)
    {
        return -ENOMEM;
    }
    if (STOW_GET(id, &value) != SUCCESS)
    {
        EVENT_UNREF(&event);
        return -EINVAL;
    }
    STOW_RELEASE(id, &value);
    EVENT_UNREF(&event);
    return 0;
}

int leak_after_success(int flag)
{
    void* buf = NULL;
    if (MEM_ALLOC(4, &buf) != 0)
    {
        return -1;
    }
    if (flag)
    {
        return 1; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_result_variable_after_success(int flag)
{
    void* buf = NULL;
    int ret = MEM_ALLOC(4, &buf);
    if (ret)
    {
        return ret;
    }
    if (flag)
    {
        return 1; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_success_arm_returns_without_release(void)
{
    void* buf = NULL;
    if (MEM_ALLOC(4, &buf) == 0)
    {
        return 0; // LEAK: buf
    }
    return -1;
}

int leak_wrong_arm_of_else(void)
{
    void* buf = NULL;
    if (MEM_ALLOC(4, &buf))
    {
        MEM_UNREF(&buf);
        return -1;
    }
    else
    {
        return 0; // LEAK: buf
    }
}

int leak_no_check(void)
{
    void* buf = NULL;
    MEM_ALLOC(4, &buf);
    return 0; // LEAK: buf
}

int leak_acquire_unconditional_reference(int flag)
{
    void* buf = get_buf();
    MEM_REF(buf);
    if (flag)
    {
        return 1; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_earlier_acquire_still_held_on_failure(void)
{
    void* buf = NULL;
    MEM_ALLOC(4, &buf);
    if (MEM_ALLOC(4, &buf) != 0)
    {
        return -1; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_loop_iteration_after_success(int count)
{
    void* buf = NULL;
    while (count--)
    {
        if (MEM_ALLOC(8, &buf) != 0)
        {
            return -1; // LEAK: buf
        }
    }
    return 0; // LEAK: buf
}

int clean_result_variable_address_passed_after_the_test(void)
{
    void* buf = NULL;
    int ret = MEM_ALLOC(4, &buf);
    if (ret != SUCCESS)
    {
        return ret;
    }
    report(&ret);
    MEM_UNREF(&buf);
    return 0;
}
