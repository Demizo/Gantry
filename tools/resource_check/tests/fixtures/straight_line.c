int clean_straight_line(void)
{
    void* buf;
    MEM_ALLOC(4, &buf);
    MEM_UNREF(buf);
    return 0;
}

int leak_straight_line(int flag)
{
    void* buf;
    MEM_ALLOC(4, &buf);
    if (flag)
    {
        return 1; // LEAK: buf
    }
    MEM_UNREF(buf);
    return 0;
}
