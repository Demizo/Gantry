"""The controller and the sensor, each client and server of the other over one link."""

from __future__ import annotations

import errno
import time

from link_broker import Action

LINK = "controller-sensor"
GET = 4
GET_RESPONSE = 5


def wait_for_frame(network, action: Action, code: int | None = None) -> None:
    """Wait until the broker has done ``action`` to a frame, with message ``code`` if given."""
    prefix = "" if code is None else f"[{code},"
    deadline = time.monotonic() + 2
    while not any(
        r.action == action and r.summary.startswith(prefix)
        for r in network.broker.frames
    ):
        assert time.monotonic() < deadline, f"no {action} frame {prefix}"
        time.sleep(0.01)


def wait_for_local(console, item: str, value: str) -> None:
    """Wait until a node's own item has a value."""
    deadline = time.monotonic() + 2
    while console.local(f"get {item}").values != {item: value}:
        assert time.monotonic() < deadline, f"{item} never became {value}"
        time.sleep(0.05)


def test_controller_reads_and_sets_sensor_items(controller, sensor):
    battery = controller.call("get Battery")
    assert battery.err == 0
    assert 0 <= int(battery.values["Battery"]) <= 100

    assert controller.call("set Mode BOOST").err == 0
    assert sensor.local("get Mode").values == {"Mode": "BOOST"}
    assert controller.call("get Mode").values == {"Mode": "BOOST"}


def test_both_nodes_are_client_and_server_over_one_link(network, controller, sensor):
    assert sensor.seen(r"Paired with controller unnamed")
    start = len(network.broker.frames)
    controller_request = controller.request("get Mode")
    sensor_request = sensor.request("get Name")
    assert controller.result(controller_request).values == {"Mode": "IDLE"}
    assert sensor.result(sensor_request).values == {"Name": "unnamed"}

    assert sensor.call("set Name kitchen").err == 0
    assert controller.local("get Name").values == {"Name": "kitchen"}

    gets = {
        (r.src, r.dst)
        for r in network.broker.frames[start:]
        if r.summary.startswith(f"[{GET},")
    }
    assert gets == {("controller", "sensor"), ("sensor", "controller")}


def test_controller_follows_battery_updates_and_saves_power(controller, sensor):
    mark = controller.mark()
    assert sensor.local("set Battery 42").err == 0
    controller.wait(r"^@update Battery=42$", since=mark)
    assert sensor.local("get Mode").values == {"Mode": "IDLE"}

    assert sensor.local("set Battery 15").err == 0
    controller.wait(r"^@update Battery=15$", since=mark)
    wait_for_local(sensor, "Mode", "ECO")


def test_cutting_the_link_disconnects_and_restoring_reconnects(
    network, controller, sensor
):
    network.broker.set_faults("controller", "sensor", delay_ms=500)
    pending = controller.request("get Mode")
    wait_for_frame(network, Action.DELAY)
    controller_mark, sensor_mark = controller.mark(), sensor.mark()
    network.link_down(LINK)

    failed = controller.wait(rf"^@done {pending} -", since=controller_mark)
    assert controller.wait(r"^@disconnected$", since=controller_mark) > failed
    sensor.wait(r"^@disconnected$", since=sensor_mark)
    wait_for_frame(network, Action.CUT, GET)
    assert controller.call("get Mode").err == -errno.ENOTCONN

    network.broker.set_faults("controller", "sensor", delay_ms=0)
    controller_mark, sensor_mark = controller.mark(), sensor.mark()
    network.link_up(LINK)
    controller.wait(r"^@connected 0$", since=controller_mark)
    sensor.wait(r"^@connected 0$", since=sensor_mark)
    controller.wait(r"Watching the sensor's battery", since=controller_mark)
    assert controller.call("get Mode").values == {"Mode": "IDLE"}
    assert sensor.call("get Name").values == {"Name": "unnamed"}

    controller_mark = controller.mark()
    assert sensor.local("set Battery 50").err == 0
    controller.wait(r"^@update Battery=50$", since=controller_mark)


def test_dropped_request_fails_only_that_request(network, controller):
    network.broker.drop_next("controller", "sensor")
    dropped = controller.request("get Mode")
    other = controller.request("get Mode")
    assert controller.result(other).values == {"Mode": "IDLE"}
    assert controller.result(dropped).err == -errno.ETIMEDOUT
    assert controller.call("get Mode").values == {"Mode": "IDLE"}


def test_dropped_response_fails_only_its_request(network, controller):
    network.broker.set_faults("sensor", "controller", drop=1.0)
    dropped = controller.request("get Mode")
    wait_for_frame(network, Action.DROP, GET_RESPONSE)
    network.broker.set_faults("sensor", "controller", drop=0.0)
    assert controller.call("get Mode").values == {"Mode": "IDLE"}
    assert controller.result(dropped).err == -errno.ETIMEDOUT


def test_delay_past_the_timeout_fails_only_that_request(network, controller):
    network.broker.set_faults("controller", "sensor", delay_ms=1500)
    late = controller.request("get Mode")
    wait_for_frame(network, Action.DELAY)
    network.broker.set_faults("controller", "sensor", delay_ms=0)
    assert controller.call("get Mode").values == {"Mode": "IDLE"}
    assert controller.result(late).err == -errno.ETIMEDOUT


def test_corrupt_request_fails_only_that_request(network, controller):
    network.broker.set_faults("controller", "sensor", corrupt=1.0)
    corrupted = controller.request("get Mode")
    wait_for_frame(network, Action.CORRUPT)
    network.broker.set_faults("controller", "sensor", corrupt=0.0)
    assert controller.call("get Mode").values == {"Mode": "IDLE"}
    assert controller.result(corrupted).err < 0


def test_duplicate_responses_are_ignored(network, controller):
    network.broker.set_faults("sensor", "controller", duplicate=1.0)
    first = controller.request("get Mode")
    assert controller.result(first).values == {"Mode": "IDLE"}
    wait_for_frame(network, Action.DUPLICATE, GET_RESPONSE)
    assert controller.call("set Mode ECO").err == 0
    assert controller.call("get Mode").values == {"Mode": "ECO"}
    assert (
        sum(line.startswith(f"@done {first} ") for line in controller.node.lines) == 1
    )
