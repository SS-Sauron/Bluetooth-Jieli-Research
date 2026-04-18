import asyncio

from bleak import BleakClient

BLE_MAC = "F4:B6:2D:AC:DA:28"


async def test():
    async with BleakClient(BLE_MAC) as client:
        print(f"[+] Connected: {client.is_connected}\n")
        for service in client.services:
            print(f"Service: {service.uuid}")
            for char in service.characteristics:
                print(f"  Char: {char.uuid}")
                print(f"  Properties: {char.properties}")
                if "read" in char.properties:
                    try:
                        value = await client.read_gatt_char(char.uuid)
                        print(f"  Value: {value.hex()}")
                    except Exception as e:
                        print(f"  Read failed: {e}")
            print()


asyncio.run(test())
