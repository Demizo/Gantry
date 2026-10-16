/*
 * Copyright (c) 2026 Demizo
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief The link to the peer and the client endpoint for its Stow
 *
 * @author Demizo (demizodemazo@gmail.com)
 */

#pragma once

#include <gantry/stow/stow_client.h>

//**********************************************************
//* Typedefs, Enums, and Structs
//**********************************************************

/**
 * @brief Client endpoint for the peer's Stow, defined by each app
 */
extern const struct stow_client_endpoint peer_client;

//**********************************************************
//* Functions
//**********************************************************

/**
 * @brief The link came up; open its server session for the peer's client
 *
 * @return SUCCESS, or the transport's error
 */
int peer_link_up(void);

/**
 * @brief Open a client session to the peer's server over the link
 *
 * @details Call once the peer's side of the link is up too.
 *
 * @return SUCCESS, or the transport's error
 */
int peer_link_connect(void);

/**
 * @brief The link went down; close its client and server sessions
 */
void peer_link_down(void);
