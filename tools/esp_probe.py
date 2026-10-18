#!/usr/bin/env python3
"""Send raw RS485 frames to the awning controller and watch the bus.

Talks to the ESP over the ESPHome native API, so Home Assistant is not
needed: frames go out through the "RS485 Raw Command" text entity, and the
device log, which shows every RX/TX frame, is printed to stdout.

Examples:
  # Who is on the bus? An unprogrammed motor answers as ID 0.
  python tools/esp_probe.py "9a 00 00 00 cc 00"

  # Program the only connected motor as ID 2 on channel 1, then verify.
  python tools/esp_probe.py "9a 02 01 00 aa aa" "9a 02 01 00 cc 00"

  # Close motor 4 for six seconds, then stop it.
  python tools/esp_probe.py "9a 04 01 00 0a ee" @6 "9a 04 01 00 0a cc"

A FRAME is "9a ID CL CH Cmd1 Cmd2" in hex; the firmware appends the
checksum. "@<seconds>" waits between frames. Without any "@" token the
frames are sent --gap seconds apart.

The API encryption key is read from $ESPHOME_API_KEY or, failing that, from
api_key in esphome/secrets.yaml.
"""
import argparse
import asyncio
import os
import re
import sys
from pathlib import Path

from aioesphomeapi import APIClient, LogLevel

SECRETS = Path(__file__).resolve().parent.parent / "esphome" / "secrets.yaml"
RAW_COMMAND_ENTITY = "rs485_raw_command"
FRAME = re.compile(r"^9a(\s+[0-9a-f]{2}){5}$", re.I)


def api_key() -> str:
    key = os.environ.get("ESPHOME_API_KEY")
    if key:
        return key
    try:
        text = SECRETS.read_text(encoding="utf-8")
    except FileNotFoundError:
        sys.exit(f"ESPHOME_API_KEY is not set and {SECRETS} does not exist")
    match = re.search(r'^api_key:\s*"?([^"\s]+)"?', text, re.M)
    if not match:
        sys.exit(f"No api_key in {SECRETS}")
    return match.group(1)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "frames", nargs="*", metavar="FRAME", help='hex frame, e.g. "9a 01 01 00 cc 00", or @<seconds>'
    )
    parser.add_argument(
        "--host",
        default=os.environ.get("ESPHOME_HOST", "awning-motor.local"),
        help="controller address (default: $ESPHOME_HOST or %(default)s)",
    )
    parser.add_argument("--port", type=int, default=6053, help="native API port (default: %(default)s)")
    parser.add_argument(
        "--gap", type=float, default=4, help="seconds between frames without @ tokens (default: %(default)s)"
    )
    parser.add_argument(
        "--listen", type=float, default=5, help="seconds to keep logging after the last frame (default: %(default)s)"
    )
    parser.add_argument(
        "--scan",
        metavar="'CL CH'",
        help="query status of IDs 01-ff on the given channel bytes, e.g. --scan '01 00'",
    )
    args = parser.parse_args()

    if args.scan:
        args.frames = [f"9a {i:02x} {args.scan} cc 00" for i in range(1, 256)]
        args.gap = 1.6  # just above the firmware's tx_delay
    for frame in args.frames:
        if frame.startswith("@"):
            try:
                float(frame[1:])
            except ValueError:
                parser.error(f"invalid pause: {frame}")
        elif not FRAME.match(frame.strip()):
            parser.error(f"invalid frame: {frame!r} (expected 6 hex bytes starting with 9a)")
    return args


async def run(args: argparse.Namespace) -> None:
    client = APIClient(args.host, args.port, None, noise_psk=api_key())
    await client.connect(login=True)
    try:
        info = await client.device_info()
        print(f"# {info.name}, ESPHome {info.esphome_version}", flush=True)

        entities, _ = await client.list_entities_services()
        text_key = next((e.key for e in entities if e.object_id == RAW_COMMAND_ENTITY), None)
        if args.frames and text_key is None:
            sys.exit(f"Entity '{RAW_COMMAND_ENTITY}' not found on {args.host}")

        client.subscribe_logs(
            lambda msg: print(msg.message.decode(errors="replace"), flush=True),
            log_level=LogLevel.LOG_LEVEL_VERY_VERBOSE,
        )
        await asyncio.sleep(1)

        explicit_pauses = any(f.startswith("@") for f in args.frames)
        for frame in args.frames:
            if frame.startswith("@"):
                await asyncio.sleep(float(frame[1:]))
                continue
            print(f"# >>> {frame}", flush=True)
            client.text_command(text_key, frame)
            if not explicit_pauses:
                await asyncio.sleep(args.gap)

        await asyncio.sleep(args.listen)
    finally:
        await client.disconnect()


if __name__ == "__main__":
    asyncio.run(run(parse_args()))
