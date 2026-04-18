import asyncio

from bleak import BleakClient

BLE_MAC = "F4:B6:2D:AC:DA:28"


def notification_handler(sender, data):
    print(f"[Notify] {sender}: {data.hex()}")


async def test():
    async with BleakClient(BLE_MAC) as client:
        print("[+] Connected — listening for notifications for 15 seconds\n")

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

        await asyncio.sleep(15)
        print("[+] Done")


asyncio.run(test())
