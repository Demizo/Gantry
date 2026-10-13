/** @acquires slot return */
static int* slot_open(void);

/** @releases slot slot */
static void slot_close(int* slot);

int clean_declaration_initializer(void)
{
    int* slot = slot_open();
    slot_close(slot);
    return 0;
}

int leak_declaration_initializer(int flag)
{
    int* slot = slot_open();
    if (flag)
    {
        return 1; // LEAK: slot
    }
    slot_close(slot);
    return 0;
}

int clean_assignment(void)
{
    int* slot;
    slot = slot_open();
    slot_close(slot);
    return 0;
}

int leak_assignment(int flag)
{
    int* slot;
    slot = slot_open();
    if (flag)
    {
        return 1; // LEAK: slot
    }
    slot_close(slot);
    return 0;
}
