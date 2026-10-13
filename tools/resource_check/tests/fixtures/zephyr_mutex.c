int clean_mutex_failure_returns_early(void)
{
    if (k_mutex_lock(&g_mutex, K_FOREVER) != 0)
    {
        return -EAGAIN;
    }
    update();
    k_mutex_unlock(&g_mutex);
    return 0;
}

int clean_mutex_result_variable(void)
{
    int ret = k_mutex_lock(&g_mutex, K_MSEC(10));
    if (ret != SUCCESS)
    {
        return ret;
    }
    update();
    k_mutex_unlock(&g_mutex);
    return 0;
}

int clean_mutex_released_on_every_path(int flag)
{
    if (k_mutex_lock(&g_mutex, K_FOREVER) == 0)
    {
        if (flag)
        {
            k_mutex_unlock(&g_mutex);
            return 1;
        }
        k_mutex_unlock(&g_mutex);
    }
    return 0;
}

int leak_mutex_held_on_early_return(int flag)
{
    if (k_mutex_lock(&g_mutex, K_FOREVER) != 0)
    {
        return -EAGAIN;
    }
    if (flag)
    {
        return 1; // LEAK: g_mutex
    }
    k_mutex_unlock(&g_mutex);
    return 0;
}

int leak_mutex_lock_not_checked(void)
{
    k_mutex_lock(&g_mutex, K_FOREVER);
    return 0; // LEAK: g_mutex
}

int leak_mutex_other_class_does_not_release(void)
{
    if (k_mutex_lock(&g_mutex, K_FOREVER) != 0)
    {
        return -EAGAIN;
    }
    MEM_UNREF(&g_mutex);
    return 0; // LEAK: g_mutex
}
