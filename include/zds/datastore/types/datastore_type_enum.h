/**
 * @file
 * @brief Implements the datastore enum type
 *
 * @author Demizo (demizodemazo@gmail.com)
 * @date 2026-03-01
 *
 *
 */

#ifndef DATASTORE_TYPE_ENUM_H
#define DATASTORE_TYPE_ENUM_H

#include <stddef.h>
#include <zds/datastore/types/datastore_types.h>

/**
 * @addtogroup datastore
 * @{
 */

//**********************************************************
//* Definitions
//**********************************************************

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief Datastore interface for enum items
 *
 * @details Operations expect to receive the address of an int
 */
extern const struct datastore_item_interface datastore_enum_interface;

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief Get the enum value's name from its numeric value
 *
 * @param[in] constraints the enum's constraints
 * @param[in] value the numeric enum value
 * @param[out] out_name the name of the enum value, only populated on success
 * @return int SUCCESS if the value existed
 * @return -EINVAL when the provided value was invalid
 */
int enum_get_name_from_value(const union datastore_constraints* constraints, int value, char** out_name);

/**
 * @brief Get the enum value's numeric value from its name
 *
 * @param[in] constraints the enum's constraints
 * @param[in] name the name of the enum value
 * @param[out] out_value numeric value of the enum value, only populated on success
 * @return int SUCCESS if the name was valid
 * @return -EINVAL when the provided name was invalid
 */
int enum_get_value_from_name(const union datastore_constraints* constraints, char* name, int* out_value);

/**
 * @}
 */

#endif  // DATASTORE_TYPE_ENUM_H
