import asyncio
import struct
from datetime import datetime

import numpy as np
from PIL import Image

from bleak import BleakClient, BleakScanner


class EPaperBLE:
    # Service UUIDs
    EPD_SERVICE_UUID = "13187b10-eba9-a3ba-044e-83d3217d9a38"
    RXTX_SERVICE_UUID = "00001f10-0000-1000-8000-00805f9b34fb"

    # Characteristic UUIDs
    EPD_CHAR_UUID = "4b646063-6264-f3a7-8941-e65356ea82fe"
    RXTX_CHAR_UUID = "00001f1f-0000-1000-8000-00805f9b34fb"

    def __init__(self):
        self.client = None
        self.connected = False

    async def connect(self, address):
        """Connect to EPaper device"""
        try:
            self.client = BleakClient(address)
            await self.client.connect()
            self.connected = True
            print(f"Connected to {address}")
        except Exception as e:
            print(f"Connection failed: {e}")
            self.connected = False

    async def disconnect(self):
        """Disconnect from device"""
        if self.client and self.connected:
            await self.client.disconnect()
            self.connected = False

    async def send_epd_command(self, cmd_bytes):
        """Send command to EPD characteristic"""
        if not self.connected:
            raise Exception("Not connected")
        await self.client.write_gatt_char(self.EPD_CHAR_UUID, cmd_bytes)

    async def send_rxtx_command(self, cmd_bytes):
        """Send command to RXTX characteristic"""
        if not self.connected:
            raise Exception("Not connected")
        await self.client.write_gatt_char(self.RXTX_CHAR_UUID, cmd_bytes)

    async def clear_screen(self, white=True):
        """Clear screen to white or black"""
        cmd = "00ff" if white else "0000"
        await self.send_epd_command(bytes.fromhex(cmd))
        await self.send_epd_command(bytes.fromhex("0101"))

    async def set_clock_mode(self, mode=1):
        """Set clock mode (1 or 2)"""
        if mode not in [1, 2, 3, 4, 5]:
            raise ValueError("Mode must be 1 or 2")
        cmd = f"e10{mode}"
        await self.send_rxtx_command(bytes.fromhex(cmd))

    async def set_image_mode(self):
        """Set to image mode"""
        await self.send_rxtx_command(bytes.fromhex("e100"))

    async def set_time(self):
        """Set device time to current time"""
        now = datetime.now()
        unix_time = int(now.timestamp())
        year = now.year
        month = now.month
        day = now.day
        week = now.isoweekday()

        time_bytes = struct.pack(">IHBBB", unix_time, year, month, day, week)

        cmd = b"\xdd" + time_bytes
        await self.send_rxtx_command(cmd)
        await self.send_rxtx_command(bytes.fromhex("e2"))

    async def upload_image(self, image_path):
        """Upload image to display"""
        # Open and convert image
        img = Image.open(image_path).convert("L")
        img = img.resize((296, 128))

        # Convert to black and white
        threshold = 128
        bw_img = img.point(lambda x: 0 if x < threshold else 255, "1")

        # Convert to bytes
        img_bytes = bytearray(bw_img.tobytes())

        # Send image data
        await self.send_epd_command(bytes.fromhex("0000"))
        await self.send_epd_command(bytes.fromhex("020000"))

        # Send in chunks of 240 bytes
        chunk_size = 240
        for i in range(0, len(img_bytes), chunk_size):
            chunk = img_bytes[i : i + chunk_size]
            position = i // 2
            header = bytes.fromhex(f"03ff{position:04x}")
            await self.send_epd_command(header + chunk)

        # Refresh display
        await self.send_epd_command(bytes.fromhex("0101"))

    @staticmethod
    async def discover():
        """Discover EPaper devices"""
        devices = await BleakScanner.discover()
        return [d for d in devices if d.name and "S24" in d.name]


# Example usage:
async def main():
    # Find devices
    devices = await EPaperBLE.discover()
    if not devices:
        print("No EPaper devices found")
        return

    # Connect to first device
    epaper = EPaperBLE()
    await epaper.connect(devices[0].address)

    # Basic operations
    await epaper.clear_screen(white=True)
    await epaper.set_time()
    await epaper.set_clock_mode(5)
    # await epaper.upload_image("test.png")

    await epaper.disconnect()


if __name__ == "__main__":
    asyncio.run(main())
