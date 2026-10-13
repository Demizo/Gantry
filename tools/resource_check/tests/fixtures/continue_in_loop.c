int clean_continue_in_loop(int count)
{
    for (int i = 0; i < count; i++)
    {
        void* buf;
        MEM_ALLOC(4, &buf);
        if (skip_item(i))
        {
            MEM_UNREF(buf);
            continue;
        }
        do_work(buf);
        MEM_UNREF(buf);
    }
    return 0;
}

int leak_continue_in_loop(int count)
{
    for (int i = 0; i < count; i++)
    {
        void* buf;
        MEM_ALLOC(4, &buf);
        if (skip_item(i))
        {
            continue;
        }
        do_work(buf);
        MEM_UNREF(buf);
    }
    return 0; // LEAK: buf
}
