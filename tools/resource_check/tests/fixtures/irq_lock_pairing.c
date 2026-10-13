int clean_irq_lock_pairing(int flag)
{
    int key = irq_lock();
    if (flag)
    {
        irq_unlock(key);
        return 1;
    }
    irq_unlock(key);
    return 0;
}

int leak_irq_lock_pairing(int flag)
{
    int key = irq_lock();
    if (flag)
    {
        return 1; // LEAK: key
    }
    irq_unlock(key);
    return 0;
}
