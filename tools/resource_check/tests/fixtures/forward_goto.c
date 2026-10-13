int clean_forward_goto(int flag)
{
    event_t* event;
    EVENT_ALLOC(&some_type, 0, &event);
    if (flag)
    {
        goto cleanup;
    }
    do_work(event);
cleanup:
    EVENT_UNREF(event);
    return 0;
}

int leak_forward_goto(int flag)
{
    event_t* event;
    EVENT_ALLOC(&some_type, 0, &event);
    if (flag)
    {
        goto skip;
    }
    EVENT_UNREF(event);
skip:
    return 0; // LEAK: event
}
