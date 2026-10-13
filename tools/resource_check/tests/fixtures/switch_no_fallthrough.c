int clean_switch_no_fallthrough(enum stow_item_id id, int kind)
{
    data_value_t value;
    STOW_GET(id, &value);
    switch (kind)
    {
        case 1:
            STOW_RELEASE(id, &value);
            break;
        case 2:
            STOW_RELEASE(id, &value);
            break;
        default:
            STOW_RELEASE(id, &value);
            break;
    }
    return 0;
}

int leak_switch_no_fallthrough(enum stow_item_id id, int kind)
{
    data_value_t value;
    STOW_GET(id, &value);
    switch (kind)
    {
        case 1:
            STOW_RELEASE(id, &value);
            break;
        case 2:
            break;
        default:
            STOW_RELEASE(id, &value);
            break;
    }
    return 0; // LEAK: value
}
