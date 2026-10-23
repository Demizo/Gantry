int leak_result_variable_reassigned_before_test(void)
{
    void* buf = NULL;
    int ret = MEM_ALLOC(4, &buf);
    ret = prepare();
    if (ret != SUCCESS)
    {
        return ret; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_result_variable_reassigned_by_compound_assignment(void)
{
    void* buf = NULL;
    int ret = MEM_ALLOC(4, &buf);
    ret |= prepare();
    if (ret != SUCCESS)
    {
        return ret; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_result_variable_incremented_before_test(void)
{
    void* buf = NULL;
    int ret = MEM_ALLOC(4, &buf);
    ret++;
    if (ret != SUCCESS)
    {
        return ret; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_result_variable_redeclared_before_test(void)
{
    void* buf = NULL;
    int ret = MEM_ALLOC(4, &buf);
    {
        int ret = prepare();
    }
    if (ret != SUCCESS)
    {
        return ret; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_result_variable_reassigned_inside_the_condition(void)
{
    void* buf = NULL;
    int ret = MEM_ALLOC(4, &buf);
    if ((ret = prepare()) != SUCCESS)
    {
        return ret; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_result_variable_address_taken(void)
{
    void* buf = NULL;
    int ret = MEM_ALLOC(4, &buf);
    refresh(&ret);
    if (ret != SUCCESS)
    {
        return ret; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_result_variable_not_local(void)
{
    void* buf = NULL;
    g_ret = MEM_ALLOC(4, &buf);
    refresh();
    if (g_ret != SUCCESS)
    {
        return g_ret; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_acquire_and_test_in_different_blocks(int flag)
{
    void* buf = NULL;
    int ret = MEM_ALLOC(4, &buf);
    if (flag)
    {
        LOG_DBG("flag");
    }
    if (ret != SUCCESS)
    {
        return ret; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_compound_condition_and(int flag)
{
    void* buf = NULL;
    if (flag && MEM_ALLOC(4, &buf) == 0)
    {
        MEM_UNREF(&buf);
    }
    return 0; // LEAK: buf
}

int leak_compound_condition_or(int flag)
{
    void* buf = NULL;
    if (MEM_ALLOC(4, &buf) != 0 || flag)
    {
        return -1; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_compound_condition_on_result_variable(int flag)
{
    void* buf = NULL;
    int ret = MEM_ALLOC(4, &buf);
    if (ret != SUCCESS || flag)
    {
        return ret; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_result_tested_by_switch(void)
{
    void* buf = NULL;
    int ret = MEM_ALLOC(4, &buf);
    switch (ret)
    {
        case 0:
            MEM_UNREF(&buf);
            return 0;
        default:
            return ret; // LEAK: buf
    }
}

int leak_acquire_tested_by_while(void)
{
    void* buf = NULL;
    while (MEM_ALLOC(4, &buf) != 0)
    {
        retry();
    }
    return 0; // LEAK: buf
}

int leak_acquire_tested_by_for(void)
{
    void* buf = NULL;
    for (; MEM_ALLOC(4, &buf) != 0;)
    {
        retry();
    }
    return 0; // LEAK: buf
}

int leak_acquire_tested_by_ternary(void)
{
    void* buf = NULL;
    int failed = MEM_ALLOC(4, &buf) ? 1 : 0;
    if (failed)
    {
        return -1; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_result_variable_tested_by_ternary(void)
{
    void* buf = NULL;
    int ret = MEM_ALLOC(4, &buf);
    int code = ret ? -1 : 0;
    if (code)
    {
        return code; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_acquire_passed_to_another_call(void)
{
    void* buf = NULL;
    if (failed(MEM_ALLOC(4, &buf)))
    {
        return -1; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_result_variable_passed_to_another_call(void)
{
    void* buf = NULL;
    int ret = MEM_ALLOC(4, &buf);
    if (failed(ret))
    {
        return ret; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_polarity_greater_than_zero(void)
{
    void* buf = NULL;
    if (MEM_ALLOC(4, &buf) > 0)
    {
        return -1; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_polarity_compared_with_error_code(void)
{
    void* buf = NULL;
    if (MEM_ALLOC(4, &buf) == -ENOMEM)
    {
        return -1; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_polarity_less_equal_zero(void)
{
    void* buf = NULL;
    if (MEM_ALLOC(4, &buf) <= 0)
    {
        return -1; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_polarity_zero_on_the_left(void)
{
    void* buf = NULL;
    if (0 != MEM_ALLOC(4, &buf))
    {
        return -1; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_polarity_unrelated_condition(int flag)
{
    void* buf = NULL;
    int ret = MEM_ALLOC(4, &buf);
    if (flag)
    {
        return ret; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_polarity_null_check_on_an_integer_result(void)
{
    void* buf = NULL;
    if (MEM_ALLOC(4, &buf) == NULL)
    {
        return -1; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_other_event_on_the_identifier_between_call_and_test(void** kept)
{
    void* buf = NULL;
    int ret = MEM_ALLOC(4, &buf);
    MEM_REF(buf);
    if (ret != SUCCESS)
    {
        return ret; // LEAK: buf
    }
    *kept = buf;
    MEM_UNREF(&buf);
    return 0;
}

int leak_result_variable_address_stored_before_test(void)
{
    void* buf = NULL;
    int ret = MEM_ALLOC(4, &buf);
    int* alias = &ret;
    *alias = 1;
    if (ret != SUCCESS)
    {
        return ret; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_result_variable_passed_to_a_macro_before_test(void)
{
    void* buf = NULL;
    int ret = MEM_ALLOC(4, &buf);
    SET_RESULT(ret);
    if (ret != SUCCESS)
    {
        return ret; // LEAK: buf
    }
    MEM_UNREF(&buf);
    return 0;
}

int leak_result_variable_address_kept_for_a_later_iteration(int count)
{
    void* buf = NULL;
    int ret;
    while (count--)
    {
        ret = MEM_ALLOC(4, &buf);
        poke();
        if (ret != SUCCESS)
        {
            return ret; // LEAK: buf
        }
        stash(&ret);
        MEM_UNREF(&buf);
    }
    return 0;
}
