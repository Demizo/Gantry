int clean_break_in_switch_in_loop(int count)
{
    void* buf;
    MEM_ALLOC(4, &buf);
    for (int i = 0; i < count; i++)
    {
        switch (i)
        {
            case 0:
                break;
            default:
                do_work(buf);
        }
    }
    MEM_UNREF(buf);
    return 0;
}

int leak_break_in_switch_in_loop(int count)
{
    void* buf;
    MEM_ALLOC(4, &buf);
    for (int i = 0; i < count; i++)
    {
        switch (i)
        {
            case 0:
                return 1; // LEAK: buf
            default:
                do_work(buf);
        }
    }
    MEM_UNREF(buf);
    return 0;
}
