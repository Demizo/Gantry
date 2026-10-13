int clean_multiple_identifiers(int flag)
{
    void* first;
    void* second;
    MEM_ALLOC(4, &first);
    MEM_ALLOC(8, &second);
    if (flag)
    {
        MEM_UNREF(first);
        MEM_UNREF(second);
        return 1;
    }
    MEM_UNREF(first);
    MEM_UNREF(second);
    return 0;
}

int leak_multiple_identifiers(int flag)
{
    void* first;
    void* second;
    MEM_ALLOC(4, &first);
    MEM_ALLOC(8, &second);
    if (flag)
    {
        MEM_UNREF(first);
        return 1; // LEAK: second
    }
    MEM_UNREF(first);
    MEM_UNREF(second);
    return 0;
}
