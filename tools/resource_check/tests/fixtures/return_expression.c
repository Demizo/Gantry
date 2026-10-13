int clean_release_in_return(void)
{
    void* buf;
    MEM_ALLOC(4, &buf);
    return MEM_UNREF(buf);
}

int leak_release_after_early_return(int flag)
{
    void* buf;
    MEM_ALLOC(4, &buf);
    if (flag)
    {
        return 1; // LEAK: buf
    }
    return MEM_UNREF(buf);
}

int clean_acquire_handed_to_caller(enum stow_item_id id, data_value_t* out_value)
{
    return stow_get(id, out_value);
}
