import asyncio

from bleak import BleakClient

BLE_MAC = "F4:B6:2D:AC:DA:28"


def notification_handler(sender, data):
    print(f"[Notify] from {sender}: {data.hex()}")


async def test():
    async with BleakClient(BLE_MAC) as client:
        print("[+] Connected\n")

        # Subscribe to all notify characteristics first
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
        print("[*] Sending probe writes to trigger responses...\n")

        # Probe the debug service
        for probe in [b"\x00", b"\x01", b"\xff", b"\xaa\x55"]:
            await client.write_gatt_char(
                "77777777-7777-7777-7777-777777777777", probe, response=False
            )
            print(f"[>] Wrote to debug service: {probe.hex()}")
            await asyncio.sleep(1)

        # Probe the companion service
        for probe in [b"\x00", b"\x01", b"\xff"]:
            await client.write_gatt_char(
                "00008888-0000-1000-8000-00805f9b34fb", probe, response=True
            )
            print(f"[>] Wrote to companion service (8888): {probe.hex()}")
            await asyncio.sleep(1)

        for probe in [b"\x00", b"\x01", b"\xff"]:
            await client.write_gatt_char(
                "00007777-0000-1000-8000-00805f9b34fb", probe, response=True
            )
            print(f"[>] Wrote to companion service (7777): {probe.hex()}")
            await asyncio.sleep(1)

        print("\n[*] Listening for 10 more seconds...")
        await asyncio.sleep(10)
        print("[+] Done")


asyncio.run(test())
