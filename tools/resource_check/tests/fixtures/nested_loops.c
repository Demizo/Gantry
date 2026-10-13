int clean_nested_loops(int outer_count, int inner_count)
{
    void* buf;
    MEM_ALLOC(4, &buf);
    for (int i = 0; i < outer_count; i++)
    {
        for (int j = 0; j < inner_count; j++)
        {
            if (skip_item(j))
            {
                continue;
            }
            if (stop_inner(j))
            {
                break;
            }
            do_work(buf);
        }
        do_outer_work(buf);
    }
    MEM_UNREF(buf);
    return 0;
}

int leak_nested_loops(int outer_count, int inner_count)
{
    void* buf;
    MEM_ALLOC(4, &buf);
    for (int i = 0; i < outer_count; i++)
    {
        for (int j = 0; j < inner_count; j++)
        {
            if (stop_inner(j))
            {
                return 1; // LEAK: buf
            }
            do_work(buf);
        }
    }
    MEM_UNREF(buf);
    return 0;
}
