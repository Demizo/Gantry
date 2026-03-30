#ifndef ERROR_H
#define ERROR_H

#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>

//**********************************************************
//* Definitions
//**********************************************************

#define SUCCESS 0

#define ASSERT(test, ...)                      \
    if (!(test))                               \
    {                                          \
        LOG_ERR("%s: %d", __FILE__, __LINE__); \
        LOG_ERR(__VA_ARGS__);                  \
                                               \
        LOG_PANIC();                           \
        k_sys_fatal_error_handler(0, NULL);    \
    }

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

//**********************************************************
//* Functions
//**********************************************************

#endif  // ERROR_H
