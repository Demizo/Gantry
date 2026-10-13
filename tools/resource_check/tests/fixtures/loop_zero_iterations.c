int clean_loop_zero_iterations(int count)
{
    void* buf;
    MEM_ALLOC(4, &buf);
    for (int i = 0; i < count; i++)
    {
        do_work(buf);
    }
    MEM_UNREF(buf);
    return 0;
}

int leak_loop_zero_iterations(int count)
{
    void* buf;
    MEM_ALLOC(4, &buf);
    for (int i = 0; i < count; i++)
    {
        MEM_UNREF(buf);
        return 0;
    }
    return 1; // LEAK: buf
}
