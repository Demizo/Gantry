int clean_infinite_loop_with_break(void)
{
    while (true)
    {
        void* buf;
        MEM_ALLOC(4, &buf);
        if (should_stop())
        {
            MEM_UNREF(buf);
            break;
        }
        do_work(buf);
        MEM_UNREF(buf);
    }
    return 0;
}

int leak_infinite_loop_with_break(void)
{
    while (true)
    {
        void* buf;
        MEM_ALLOC(4, &buf);
        if (should_stop())
        {
            break;
        }
        do_work(buf);
        MEM_UNREF(buf);
    }
    return 0; // LEAK: buf
}

void clean_thread_holding_a_resource_in_a_while_true_loop(void)
{
    void* buf;
    MEM_ALLOC(4, &buf);
    while (true)
    {
        do_work(buf);
    }
}

void clean_thread_holding_a_resource_in_a_while_1_loop(void)
{
    void* buf;
    MEM_ALLOC(4, &buf);
    while (1)
    {
        do_work(buf);
    }
}

void clean_thread_holding_a_resource_in_a_do_while_1_loop(void)
{
    void* buf;
    MEM_ALLOC(4, &buf);
    do
    {
        do_work(buf);
    } while (1);
}
