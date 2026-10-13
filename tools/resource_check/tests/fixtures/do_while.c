int clean_do_while(int count)
{
    void* buf;
    MEM_ALLOC(4, &buf);
    int i = 0;
    do
    {
        do_work(buf);
        i++;
    } while (i < count);
    MEM_UNREF(buf);
    return 0;
}

int leak_do_while(int count)
{
    void* buf;
    int i = 0;
    do
    {
        MEM_ALLOC(4, &buf);
        i++;
        if (i > count)
        {
            return 1; // LEAK: buf
        }
        MEM_UNREF(buf);
    } while (true);
    return 0;
}
