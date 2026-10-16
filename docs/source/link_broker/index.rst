===========
Link Broker
===========

Overview
========

The link broker tests several nodes together without hardware. Each node runs its real firmware as a ``native_sim`` process, with the UART to its peers on a pseudo-terminal. The broker joins those UARTs as a topology file describes, injects faults on each link and logs every frame. It lives in ``tools/link_broker`` and runs on Linux, or on Windows in WSL, inside the repository's ``nix develop`` shell.

Topology
========

A topology names the nodes, each built from a Zephyr app, and the point-to-point links between them:

.. code-block:: yaml

    seed: 1
    nodes:
      controller:
        app: controller
      sensor:
        app: sensor
    links:
      - ends: [controller, sensor]
        faults:
          controller->sensor: {drop: 0.1, delay_ms: 20}

Each node's link is the PTY of its second UART, ``uart_1``. A link is named after its ends, ``controller-sensor`` here. App paths are relative to the topology file.

Faults
======

Faults apply to each direction of a link. ``drop``, ``duplicate`` and ``corrupt`` are probabilities per frame, and ``delay_ms`` holds every frame back. Each direction draws its random faults from its own generator, so with a ``seed`` in the topology they repeat for the same frames. A corrupted frame has one bit flipped. A test can also change faults, drop the next frames, or cut and restore a link while it runs. Cutting a link drops everything on it, including delayed frames.

Frames are split on the COBS delimiter. A lone delimiter is line idle: it is forwarded unless the link is cut, but never faulted or logged.

Frame Log
=========

Every frame is logged with its link, direction and what the broker did with it. A frame is COBS-decoded and its CBOR printed in diagnostic notation, so the log needs no knowledge of the protocol:

.. code-block:: text

        0.014 controller-sensor controller->sensor forward   [0,0]
        0.020 controller-sensor sensor->controller forward   [1,0,1,{0:512,1:"122c...",2:65535,3:[]}]

Tests
=====

The broker's pytest plugin builds each node with ``west build`` for ``native_sim/native/64`` once per session. A suite names its topology by overriding the ``link_topology_path`` fixture. The ``network`` fixture starts every node and the broker for each test and brings every link up. Each test's frame log and node consoles are kept in its temporary directory.

Raw COBS has no link layer, so the harness stands in for the app's link manager, using each node's console:

- ``link up``: the link came up. The node opens its server session and answers ``@link up <err>``.
- ``peer connect``: once both ends are up, the node opens its client session.
- ``link down``: the link went down. The node closes its sessions and answers ``@link down``.

``network.link_down(name)`` and ``network.link_up(name)`` cut or restore a link in the broker and tell the nodes on it.

The ``samples/stow_peers`` controller and sensor are the first such network. Each is the other's Stow client and server over one link:

.. code-block:: sh

    just test-peers
