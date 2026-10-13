/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Annotated prototypes of Zephyr APIs, read by the resource checker. This file is only scanned, never compiled or
 * included, so the declarations follow Zephyr's public parameter names but need no includes.
 */

/** @acquires irq return */
unsigned int irq_lock(void);

/** @releases irq key */
void irq_unlock(unsigned int key);
