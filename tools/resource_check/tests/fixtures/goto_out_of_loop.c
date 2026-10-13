int clean_goto_out_of_loop(int count)
{
    void* buf;
    MEM_ALLOC(4, &buf);
    for (int i = 0; i < count; i++)
    {
        if (should_abort(i))
        {
            goto cleanup;
        }
        do_work(buf);
    }
cleanup:
    MEM_UNREF(buf);
    return 0;
}

int leak_goto_out_of_loop(int count)
{
    void* buf;
    MEM_ALLOC(4, &buf);
    for (int i = 0; i < count; i++)
    {
        if (should_abort(i))
        {
            goto done;
        }
        do_work(buf);
    }
    MEM_UNREF(buf);
done:
    return 0; // LEAK: buf
}
