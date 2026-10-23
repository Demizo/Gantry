"""The protocol's golden vectors, decoded by the client's codec and played against a real client."""

from __future__ import annotations

import asyncio
import dataclasses
from collections.abc import Awaitable
from pathlib import Path
from typing import Any

import cbor_diag
import pytest

from protocol_spec import Step, Vector, load
from stow_client import (
    DescribeRequest,
    DescribeResponse,
    Error,
    GetRequest,
    GetResponse,
    HelloRequest,
    HelloResponse,
    Message,
    Ok,
    Response,
    SetRequest,
    StowClient,
    StowError,
    StowTransport,
    SubscribeRequest,
    UnsubscribeRequest,
    Update,
    decode,
    decode_value,
    encode_value,
    parse_description,
)

VECTORS = load(Path(__file__).resolve().parents[3] / "protocols" / "stow")
CLIENT = 0


def _names(vectors: tuple[Vector, ...]) -> list[str]:
    return [vector.name for vector in vectors]


def test_the_sides_are_client_then_server():
    assert VECTORS.sides == ("client", "server")


@pytest.mark.parametrize(
    "vector",
    VECTORS.exchanges + VECTORS.malformed,
    ids=_names(VECTORS.exchanges + VECTORS.malformed),
)
def test_every_message_round_trips(vector: Vector):
    malformed = vector in VECTORS.malformed
    for index, step in enumerate(vector.steps):
        if malformed and step.side == CLIENT:
            continue
        assert decode(step.data).encode() == step.data, (
            f"{vector.name}.steps[{index}]: expected {step.edn}"
        )


class ExchangeTransport(StowTransport):
    """Checks the client's sends against an exchange and answers with its server steps."""

    def __init__(self) -> None:
        self.steps: tuple[Step, ...] = ()
        self.position = 0
        self.extra: list[bytes] = []
        self._name = ""
        self._tags: dict[int, int] = {}
        self._incoming: asyncio.Queue[bytes] = asyncio.Queue()
        self._waiting = False

    def play(self, vector: Vector) -> None:
        self.steps = vector.steps
        self.position = 0
        self._name = vector.name
        self._tags = {}
        self._deliver()

    async def send(self, data: bytes) -> None:
        if self.position == len(self.steps):
            self.extra.append(data)
            return
        step = self.steps[self.position]
        where = f"{self._name}.steps[{self.position}]"
        sent = decode(data)
        assert step.side == CLIENT, f"{where}: client sent {cbor_diag.cbor2diag(data)}"
        expected = decode(step.data)
        self._tags[expected.tag] = sent.tag
        assert dataclasses.replace(sent, tag=expected.tag).encode() == step.data, (
            f"{where}: expected {step.edn}, client sent {cbor_diag.cbor2diag(data)}"
        )
        self.position += 1
        self._deliver()

    async def receive(self) -> bytes:
        self._waiting = True
        data = await self._incoming.get()
        self._waiting = False
        return data

    async def close(self) -> None:
        pass

    def idle(self) -> bool:
        """Whether the client has handled every message delivered and waits for the next."""
        return self._waiting and self._incoming.empty()

    def _deliver(self) -> None:
        while (
            self.position < len(self.steps) and self.steps[self.position].side != CLIENT
        ):
            message = decode(self.steps[self.position].data)
            if isinstance(message, Response) and message.tag is not None:
                message = dataclasses.replace(message, tag=self._tags[message.tag])
            self._incoming.put_nowait(message.encode())
            self.position += 1


def _is_load(steps: tuple[Step, ...], position: int) -> bool:
    """Whether the adapter plays the Hello at position with load()."""
    later = [decode(s.data) for s in steps[position + 1 :] if s.side == CLIENT]
    return bool(later) and later[0] == DescribeRequest(tag=later[0].tag)


class Player:
    """Makes the public call that sends each client step and checks its outcome against the server steps."""

    def __init__(self, client: StowClient, transport: ExchangeTransport) -> None:
        self.client = client
        self.transport = transport
        self.updates: list[Update] = []

    def _names(self, item_ids: tuple[int, ...]) -> list[str]:
        by_id = {item.id: item.name for item in self.client.get_all_items()}
        unknown = [item_id for item_id in item_ids if item_id not in by_id]
        if unknown:
            pytest.skip(
                f"no public call names item {unknown[0]}, which is not described"
            )
        return [by_id[item_id] for item_id in item_ids]

    def call(self, steps: tuple[Step, ...], position: int) -> Awaitable[Any]:
        request = decode(steps[position].data)
        match request:
            case HelloRequest():
                return (
                    self.client.load()
                    if _is_load(steps, position)
                    else self.client.hello()
                )
            case DescribeRequest(names=None):
                pytest.skip("only load() sends an unfiltered Describe, after its Hello")
            case DescribeRequest(start_item_id=start) if start != 0:
                pytest.skip("no public call sends a Describe from a later item")
            case DescribeRequest(names=names):
                return self.client.describe_items(names)
            case GetRequest(item_ids=ids) if len(set(ids)) < len(ids):
                pytest.skip("no public call sends a Get naming one item twice")
            case GetRequest(item_ids=ids):
                return self.client.get(self._names(ids))
            case SetRequest(items=items) if len({i for i, _ in items}) < len(items):
                pytest.skip("no public call sends a Set naming one item twice")
            case SetRequest(items=items):
                names = self._names(tuple(item_id for item_id, _ in items))
                values = {
                    name: self._decode(name, value)
                    for name, (_, value) in zip(names, items, strict=True)
                }
                return self.client.set(values)
            case SubscribeRequest(item_ids=ids):
                return self.client.subscribe(
                    self._names(ids), listener=self.updates.append
                )
            case UnsubscribeRequest(item_ids=ids):
                return self.client.unsubscribe(self._names(ids))
        pytest.skip(f"no public call sends a {type(request).__name__}")

    def _decode(self, name: str, value: Any) -> Any:
        item = self.client.get_item_description(name)
        return decode_value(item.type, item.constraints, value)

    def _encode(self, item_id: int, value: Any) -> Any:
        item = next(i for i in self.client.get_all_items() if i.id == item_id)
        return encode_value(item.type, item.constraints, value)

    def check(self, outcome: Any, answers: list[Message]) -> None:
        responses = [m for m in answers if not isinstance(m, Update)]
        last = responses[-1]
        if isinstance(last, Error):
            assert isinstance(outcome, StowError), outcome
            assert (outcome.code, outcome.item, outcome.index) == (
                last.code,
                last.item,
                last.index,
            )
        else:
            self._check_values(outcome, responses)
        received = [
            Update(item_id=u.item_id, value=self._encode(u.item_id, u.value)).encode()
            for u in self.updates
        ]
        assert received == [m.encode() for m in answers if isinstance(m, Update)]
        self.updates.clear()

    def _check_values(self, outcome: Any, responses: list[Message]) -> None:
        chunks = [m.chunk for m in responses if isinstance(m, DescribeResponse)]
        for response in responses:
            match response:
                case HelloResponse():
                    hello = self.client.hello_response
                    assert dataclasses.replace(hello, tag=response.tag) == response
                case GetResponse(items=items):
                    names = {i.id: i.name for i in self.client.get_all_items()}
                    returned = tuple(
                        (item_id, self._encode(item_id, outcome[names[item_id]]))
                        for item_id, _ in items
                    )
                    assert dataclasses.replace(response, items=returned).encode() == (
                        response.encode()
                    )
                case Ok():
                    assert outcome is None
        if chunks:
            hello = self.client.hello_response
            described = parse_description(b"".join(chunks), hello.role_names)
            assert (
                outcome if outcome is not None else self.client.get_all_items()
            ) == (described)

    async def play(self, vector: Vector) -> None:
        self.transport.play(vector)
        while self.transport.position < len(vector.steps):
            start = self.transport.position
            call = self.call(vector.steps, start)
            try:
                outcome = await call
            except StowError as error:
                outcome = error
            await self._drain()
            steps = vector.steps[start : self.transport.position]
            self.check(outcome, [decode(s.data) for s in steps if s.side != CLIENT])
        await self._drain()
        assert self.transport.extra == [], (
            f"{vector.name}: client sent more after the last step"
        )

    async def _drain(self) -> None:
        while not self.transport.idle():
            await asyncio.sleep(0)


# The adapter plays this exchange first, so the client has its descriptions
CONNECT = next(v for v in VECTORS.exchanges if _is_load(v.steps, 0))


@pytest.mark.parametrize("exchange", VECTORS.exchanges, ids=_names(VECTORS.exchanges))
async def test_client_plays_the_exchange(exchange: Vector, tmp_path: Path):
    transport = ExchangeTransport()
    client = StowClient(transport, cache_dir=tmp_path, response_timeout=1.0)
    player = Player(client, transport)
    try:
        if exchange is not CONNECT:
            await player.play(CONNECT)
        await player.play(exchange)
    finally:
        await client.close()
