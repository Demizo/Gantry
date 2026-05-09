#include <stddef.h>
#include <sys/errno.h>

size_t strscpy(char* dest, const char* src, size_t count)
{
    size_t res = 0;

    if (count == 0)
    {
        return -E2BIG;
    }

    while (res < count)
    {
        dest[res] = src[res];
        if (dest[res] == '\0')
        {
            return res;
        }
        res++;
    }

    // If we reach here, we ran out of space.
    // Force null-termination at the very end of the buffer.
    dest[count - 1] = '\0';
    return -E2BIG;
}