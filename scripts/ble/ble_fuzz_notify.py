import asyncio

from bleak import BleakClient

BLE_MAC = "F4:B6:2D:AC:DA:28"

# Common command formats used by Jieli and similar audio chipset vendors
# Each is a (description, characteristic_uuid, payload) tuple
PROBES = [
    # Jieli common header formats
    (
        "Jieli AA55 header",
        "00008888-0000-1000-8000-00805f9b34fb",
        bytes([0xAA, 0x55, 0x00, 0x00]),
    ),
    (
        "Jieli AA55 + cmd 01",
        "00008888-0000-1000-8000-00805f9b34fb",
        bytes([0xAA, 0x55, 0x01, 0x00]),
    ),
    (
        "Jieli AA55 + cmd 02",
        "00008888-0000-1000-8000-00805f9b34fb",
        bytes([0xAA, 0x55, 0x02, 0x00]),
    ),
    (
        "Jieli AA55 + len",
        "00008888-0000-1000-8000-00805f9b34fb",
        bytes([0xAA, 0x55, 0x00, 0x01, 0x00]),
    ),
    # Length-prefixed formats
    ("Len prefix 0x01", "00008888-0000-1000-8000-00805f9b34fb", bytes([0x01, 0x00])),
    (
        "Len prefix 0x02",
        "00008888-0000-1000-8000-00805f9b34fb",
        bytes([0x02, 0x01, 0x00]),
    ),
    (
        "Len prefix 0x03",
        "00008888-0000-1000-8000-00805f9b34fb",
        bytes([0x03, 0x01, 0x00, 0x00]),
    ),
    # Soundcore app common patterns
    (
        "Soundcore cmd battery",
        "00008888-0000-1000-8000-00805f9b34fb",
        bytes([0x08, 0xEE, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00]),
    ),
    (
        "Soundcore cmd ANC",
        "00008888-0000-1000-8000-00805f9b34fb",
        bytes([0x08, 0xEE, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00]),
    ),
    (
        "Soundcore cmd EQ",
        "00008888-0000-1000-8000-00805f9b34fb",
        bytes([0x08, 0xEE, 0x00, 0x00, 0x00, 0x03, 0x03, 0x00]),
    ),
    # 7777 characteristic
    (
        "7777 AA55",
        "00007777-0000-1000-8000-00805f9b34fb",
        bytes([0xAA, 0x55, 0x00, 0x00]),
    ),
    (
        "7777 cmd 01",
        "00007777-0000-1000-8000-00805f9b34fb",
        bytes([0x01, 0x00, 0x00, 0x00]),
    ),
    # Debug service
    (
        "Debug AA55",
        "77777777-7777-7777-7777-777777777777",
        bytes([0xAA, 0x55, 0x00, 0x00]),
    ),
    (
        "Debug cmd 01",
        "77777777-7777-7777-7777-777777777777",
        bytes([0x01, 0x00, 0x00, 0x00]),
    ),
    (
        "Debug FF",
        "77777777-7777-7777-7777-777777777777",
        bytes([0xFF, 0x00, 0x00, 0x00]),
    ),
]

responses = []


def notification_handler(sender, data):
    print(f"\n[!!!] NOTIFICATION from {sender}: {data.hex()}")
    responses.append((str(sender), data.hex()))


async def test():
    async with BleakClient(BLE_MAC) as client:
        print("[+] Connected\n")

        # Subscribe to all notify characteristics
        await client.start_notify(
            "77777777-7777-7777-7777-777777777777", notification_handler
        )
        await client.start_notify(
            "00008888-0000-1000-8000-00805f9b34fb", notification_handler
        )
        await client.start_notify(
            "00007777-0000-1000-8000-00805f9b34fb", notification_handler
        )
        await client.start_notify(
            "0000ae02-0000-1000-8000-00805f9b34fb", notification_handler
        )

        print("[*] Subscribed to all notify characteristics")
        print("[*] Starting structured probe sequence...\n")

        for desc, char_uuid, payload in PROBES:
            print(f"[>] {desc}: {payload.hex()}")
            try:
                response_flag = "write" in char_uuid
                await client.write_gatt_char(char_uuid, payload, response=False)
            except Exception as e:
                print(f"    Write error: {e}")
            await asyncio.sleep(1.5)

        print("\n[*] Probe sequence complete")
        print(f"[*] Total notifications received: {len(responses)}")
        if responses:
            print("\n=== Responses ===")
            for sender, data in responses:
                print(f"{sender}: {data}")


asyncio.run(test())
