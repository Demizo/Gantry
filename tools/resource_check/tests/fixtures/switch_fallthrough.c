int clean_switch_fallthrough(int kind)
{
    void* buf;
    MEM_ALLOC(4, &buf);
    switch (kind)
    {
        case 1:
        case 2:
            do_work(buf);
            break;
        default:
            do_other_work(buf);
    }
    MEM_UNREF(buf);
    return 0;
}

int leak_switch_fallthrough(int kind)
{
    void* buf;
    MEM_ALLOC(4, &buf);
    switch (kind)
    {
        case 1:
        case 2:
            MEM_UNREF(buf);
            break;
        default:
            break;
    }
    return 0; // LEAK: buf
}
