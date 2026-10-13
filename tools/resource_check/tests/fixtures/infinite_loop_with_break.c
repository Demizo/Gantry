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
