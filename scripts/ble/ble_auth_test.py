import asyncio

from bleak import BleakClient

BLE_MAC = "F4:B6:2D:AC:DA:28"


async def test():
    async with BleakClient(BLE_MAC) as client:
        print("[+] Connected\n")

        # Test 1 — Debug service write
        print("[*] Testing debug service write (66666666)...")
        try:
            await client.write_gatt_char(
                "77777777-7777-7777-7777-777777777777", bytes([0x00]), response=False
            )
            print("[+] Write ACCEPTED — no authentication required")
        except Exception as e:
            print(f"[-] Write REJECTED: {e}")

        # Test 2 — Companion service write (8888)
        print("\n[*] Testing companion service write (8888)...")
        try:
            await client.write_gatt_char(
                "00008888-0000-1000-8000-00805f9b34fb", bytes([0x00]), response=True
            )
            print("[+] Write ACCEPTED — no authentication required")
        except Exception as e:
            print(f"[-] Write REJECTED: {e}")

        # Test 3 — OTA service write (ae01)
        # Sending a single null byte — safe probe, not actual firmware data
        print("\n[*] Testing OTA service write (ae01)...")
        try:
            await client.write_gatt_char(
                "0000ae01-0000-1000-8000-00805f9b34fb", bytes([0x00]), response=False
            )
            print("[+] Write ACCEPTED — no authentication required")
        except Exception as e:
            print(f"[-] Write REJECTED: {e}")


asyncio.run(test())
