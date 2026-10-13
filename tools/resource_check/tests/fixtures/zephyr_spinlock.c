int clean_spinlock(void)
{
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    update();
    k_spin_unlock(&g_lock, key);
    return 0;
}

int clean_spinlock_released_on_every_path(int flag)
{
    k_spinlock_key_t key;
    key = k_spin_lock(&g_lock);
    if (flag)
    {
        k_spin_unlock(&g_lock, key);
        return 1;
    }
    k_spin_unlock(&g_lock, key);
    return 0;
}

int leak_spinlock_early_return(int flag)
{
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    if (flag)
    {
        return 1; // LEAK: key
    }
    k_spin_unlock(&g_lock, key);
    return 0;
}

int leak_spinlock_never_released(void)
{
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    update();
    return 0; // LEAK: key
}

int leak_irq_key_does_not_release_a_spinlock(void)
{
    k_spinlock_key_t key = k_spin_lock(&g_lock);
    irq_unlock(key);
    return 0; // LEAK: key
}
