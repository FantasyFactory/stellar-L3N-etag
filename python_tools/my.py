import asyncio

from bleak import BleakClient

# UUID delle caratteristiche da leggere
TEMP_UUID = "00002a1f-0000-1000-8000-00805f9b34fb"
BATT_UUID = "00002a19-0000-1000-8000-00805f9b34fb"


async def main(address):
    async with BleakClient(address) as client:
        temp_bytes = await client.read_gatt_char(TEMP_UUID)
        batt_bytes = await client.read_gatt_char(BATT_UUID)

        # Converti i bytes in valori
        temp = int.from_bytes(temp_bytes, byteorder="little") / 10  # Temperatura in °C
        batt = int.from_bytes(batt_bytes, byteorder="little")  # Livello batteria in %

        print(f"Temperatura: {temp}°C")
        print(f"Batteria: {batt}%")


if __name__ == "__main__":
    # Sostituisci con l'indirizzo del tuo device
    device_address = "C9:88:00:7A:27:AC"
    asyncio.run(main(device_address))
