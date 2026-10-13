int clean_backward_goto(int tries)
{
    void* buf;
retry:
    MEM_ALLOC(4, &buf);
    if (should_retry(tries))
    {
        MEM_UNREF(buf);
        tries--;
        goto retry;
    }
    MEM_UNREF(buf);
    return 0;
}

int leak_backward_goto(int tries)
{
    void* buf;
retry:
    MEM_ALLOC(4, &buf);
    if (should_retry(tries))
    {
        tries--;
        goto retry;
    }
    return 0; // LEAK: buf
}
