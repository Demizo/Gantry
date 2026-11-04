struct codec
{
    /** @acquires memory out_value on-success */
    int (*get)(void* src, data_value_t* out_value);
    /** @releases memory value */
    void (*release)(data_value_t* value);
    int (*copy)(void* src, data_value_t* out_value);
};

static int get(void* src, data_value_t* out_value)
{
    *out_value = *(data_value_t*)src;
    return SUCCESS;
}

static void release(data_value_t* value) { ARG_UNUSED(value); }

static const struct codec int_codec = {
    .get = get,
    .release = release,
};

int clean_get_then_release(const struct codec* codec, void* src)
{
    data_value_t value;
    if (codec->get(src, &value) != SUCCESS)
    {
        return -EINVAL;
    }
    codec->release(&value);
    return 0;
}

int get_leaks(void* src, int flag)
{
    data_value_t value;
    if (int_codec.get(src, &value) != SUCCESS)
    {
        return -EINVAL;
    }
    if (flag)
    {
        return 1; // LEAK: value
    }
    int_codec.release(&value);
    return 0;
}

int released_twice(const struct codec* codec, void* src)
{
    data_value_t value;
    if (codec->get(src, &value) != SUCCESS)
    {
        return -EINVAL;
    }
    codec->release(&value);
    codec->release(&value); // RELEASED TWICE: value
    return 0;
}

int clean_untagged_field_call(const struct codec* codec, void* src)
{
    data_value_t value;
    codec->copy(src, &value);
    return 0;
}
