#ifndef FLAGS_H
#define FLAGS_H

#include <zephyr/sys/atomic.h>

//**********************************************************
//* Definitions
//**********************************************************

#define NO_FLAG_RULES ;

// CONFIG_FLAGS_VALIDATION controls whether flag rules will be validated
// whenever flags change.
#ifdef CONFIG_FLAGS_VALIDATION

#define FLAGS_DEFINE(name, flags, rules)                \
    enum name##_flags{ __DEBRACKET flags, name##_MAX }; \
    ATOMIC_DEFINE(name, name##_MAX);                    \
    static inline void name##_validate(void)            \
    {                                                   \
        atomic_t* name_ptr = name;                      \
        (void)name_ptr;                                 \
        rules                                           \
    }

#define _VALIDATE_FLAGS(name) name##_validate()

#define FLAG_REQUIRES(flag1, flag2)                                        \
    if (CHECK_FLAG(name_ptr, flag1) && !CHECK_FLAG(name_ptr, flag2))       \
    {                                                                      \
        __ASSERT(false, "Flag Violation: %s requires %s", #flag1, #flag2); \
    }

#define FLAG_EXCLUSIVE(flag1, flag2)                                                         \
    if (CHECK_FLAG(name_ptr, flag1) && CHECK_FLAG(name_ptr, flag2))                          \
    {                                                                                        \
        __ASSERT(false, "Flag Violation: %s and %s are mutually exclusive", #flag1, #flag2); \
    }

#else

#define FLAGS_DEFINE(name, flags, rules)                \
    enum name##_flags{ __DEBRACKET flags, name##_MAX }; \
    ATOMIC_DEFINE(name, name##_MAX);

#define _VALIDATE_FLAGS(name) ((void)0)
#define FLAG_REQUIRES(flag1, flag2)
#define FLAG_EXCLUSIVE(flag1, flag2)
#endif

#define SET_FLAG(name, flag)                         \
    atomic_set_bit(name, flag);                      \
    LOG_DBG("Set " #flag ": %lu", atomic_get(name)); \
    _VALIDATE_FLAGS(name);

#define CLEAR_FLAG(name, flag)                           \
    atomic_clear_bit(name, flag);                        \
    LOG_DBG("Cleared " #flag ": %lu", atomic_get(name)); \
    _VALIDATE_FLAGS(name);

#define CHECK_FLAG(name, flag) atomic_test_bit(name, flag)

#endif  // FLAGS_H
