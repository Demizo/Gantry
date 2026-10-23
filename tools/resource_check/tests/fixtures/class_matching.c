int clean_pass_ownership_releases_any_class(int flag)
{
    int key = irq_lock();
    if (flag)
    {
        PASS_OWNERSHIP(key);
        return 1;
    }
    irq_unlock(key);
    return 0;
}

int leak_release_of_other_class(void)
{
    int key = irq_lock();
    MEM_UNREF(key);
    return 0; // LEAK: key
}

int leak_memory_released_with_irq_unlock(void)
{
    void* buf;
    MEM_ALLOC(4, &buf);
    irq_unlock(buf);
    return 0; // LEAK: buf
}

int one_identifier_two_classes(void)
{
    unsigned int res = irq_lock();
    MEM_ALLOC(4, &res);
    MEM_UNREF(&res);
    irq_unlock(res); // USED AFTER RELEASE: res
    return 0;
}

int clean_pass_ownership_releases_every_class(void)
{
    unsigned int res = irq_lock();
    MEM_ALLOC(4, &res);
    PASS_OWNERSHIP(res);
    return 0;
}
