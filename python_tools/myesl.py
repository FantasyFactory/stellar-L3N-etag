import asyncio
import argparse
from bleak import BleakClient, BleakScanner
from datetime import datetime
import struct
from PIL import Image
import numpy as np
from enum import Enum
#from typing import Optional


class DitherMode(Enum):
    NONE = "none"
    FLOYDSTEINBERG = "floydsteinberg"
    BAYER = "bayer"
    ATKINSON = "atkinson"


class EPaperBLE:
    EPD_SERVICE_UUID = "13187b10-eba9-a3ba-044e-83d3217d9a38"
    RXTX_SERVICE_UUID = "00001f10-0000-1000-8000-00805f9b34fb"
    EPD_CHAR_UUID = "4b646063-6264-f3a7-8941-e65356ea82fe"
    RXTX_CHAR_UUID = "00001f1f-0000-1000-8000-00805f9b34fb"
    TEMP_CHAR_UUID = "00002a1f-0000-1000-8000-00805f9b34fb"
    BATT_CHAR_UUID = "00002a19-0000-1000-8000-00805f9b34fb"

    def __init__(self):
        self.client = None
        self.connected = False

    async def connect(self, address):
        try:
            self.client = BleakClient(address)
            await self.client.connect()
            self.connected = True
            print(f"Connected to {address}")
            return True
        except Exception as e:
            print(f"Connection failed: {e}")
            self.connected = False
            return False

    async def disconnect(self):
        if self.client and self.connected:
            await self.client.disconnect()
            self.connected = False

    async def send_epd_command(self, cmd_bytes):
        if not self.connected:
            raise Exception("Not connected")
        await self.client.write_gatt_char(self.EPD_CHAR_UUID, cmd_bytes)

    async def send_rxtx_command(self, cmd_bytes):
        if not self.connected:
            raise Exception("Not connected")
        await self.client.write_gatt_char(self.RXTX_CHAR_UUID, cmd_bytes)

    async def get_temperature(self) -> float:
        """Read temperature in Celsius"""
        temp_bytes = await self.client.read_gatt_char(self.TEMP_CHAR_UUID)
        temp = int.from_bytes(temp_bytes, byteorder='little') / 10
        return temp

    async def get_battery(self) -> int:
        """Read battery percentage"""
        batt_bytes = await self.client.read_gatt_char(self.BATT_CHAR_UUID)
        batt = int.from_bytes(batt_bytes, byteorder='little')
        return batt

    async def set_mode(self, mode: int):
        """Set display mode (0-5)"""
        if not 0 <= mode <= 5:
            raise ValueError("Mode must be between 0 and 5")
        await self.send_rxtx_command(bytes.fromhex(f"e1{mode:02d}"))

    async def screen_black(self):
        await self.send_epd_command(bytes.fromhex("0000"))

    async def screen_white(self):
        await self.send_epd_command(bytes.fromhex("00ff"))

    async def screen_refresh(self):
        await self.send_epd_command(bytes.fromhex("01"))

    async def full_picture_refresh(self):
        await self.send_epd_command(bytes.fromhex("0100"))

    async def partial_picture_refresh(self):
        await self.send_epd_command(bytes.fromhex("0101"))

    async def blink(self, color: int):
        """Set LED color (1-7)
        1=Red, 2=Green, 3=Blue, 4=Yellow, 5=Aqua, 6=Magenta, 7=White, 0=Off"""
        if not 0 <= color <= 7:
            raise ValueError("Color must be between 0 and 7")
        await self.send_rxtx_command(bytes.fromhex(f"81{color:02x}"))

    async def set_datetime(self, offset: int = 0):
        now = datetime.now()
        unix_time = int(now.timestamp()) + (offset * 3600)  # Add offset hours
        year = now.year
        month = now.month
        day = now.day
        week = now.isoweekday()

        time_bytes = struct.pack(">IHBBB", unix_time, year, month, day, week)
        cmd = b"\xdd" + time_bytes
        await self.send_rxtx_command(cmd)
        await self.send_rxtx_command(bytes.fromhex("e2"))

    async def upload_picture(self, image_path: str, dither_mode: DitherMode = DitherMode.NONE):
        img = Image.open(image_path).convert('L')
        img = img.resize((296, 128))

        if dither_mode != DitherMode.NONE:
            if dither_mode == DitherMode.FLOYDSTEINBERG:
                img = img.convert('1', dither=Image.FLOYDSTEINBERG)
            elif dither_mode == DitherMode.BAYER:
                # Implementa dithering Bayer
                threshold_map = np.array([[0, 8, 2, 10],
                                       [12, 4, 14, 6],
                                       [3, 11, 1, 9],
                                       [15, 7, 13, 5]]) * 16
                img = img.point(lambda x: 255 if x > threshold_map[x % 4][x % 4] else 0, '1')
            elif dither_mode == DitherMode.ATKINSON:
                # Implementa dithering Atkinson
                img_array = np.array(img)
                height, width = img_array.shape
                for y in range(height):
                    for x in range(width):
                        old_pixel = img_array[y, x]
                        new_pixel = 255 if old_pixel > 128 else 0
                        img_array[y, x] = new_pixel
                        error = old_pixel - new_pixel
                        if x + 1 < width:
                            img_array[y, x + 1] += error * 1/8
                        if x + 2 < width:
                            img_array[y, x + 2] += error * 1/8
                        if y + 1 < height:
                            img_array[y + 1, x] += error * 1/8
                            if x + 1 < width:
                                img_array[y + 1, x + 1] += error * 1/8
                            if x - 1 >= 0:
                                img_array[y + 1, x - 1] += error * 1/8
                        if y + 2 < height:
                            img_array[y + 2, x] += error * 1/8
                img = Image.fromarray(img_array).convert('1')
        else:
            img = img.point(lambda x: 0 if x < 128 else 255, '1')

        img_bytes = bytearray(img.tobytes())

        await self.send_epd_command(bytes.fromhex("0000"))
        await self.send_epd_command(bytes.fromhex("020000"))

        chunk_size = 240
        for i in range(0, len(img_bytes), chunk_size):
            chunk = img_bytes[i:i + chunk_size]
            position = i // 2
            header = bytes.fromhex(f"03ff{position:04x}")
            await self.send_epd_command(header + chunk)

        await self.partial_picture_refresh()

    @staticmethod
    async def discover():
        devices = await BleakScanner.discover()
        return [d for d in devices if d.name and "S24" in d.name]


async def main():
    parser = argparse.ArgumentParser(description='EPaper BLE Controller')
    parser.add_argument('-scan', action='store_true', help='Scan for EPaper devices')
    parser.add_argument('-address', help='BLE device address')
    parser.add_argument('-name', help='BLE device name')
    parser.add_argument('-setmode', type=int, choices=range(6), help='Set display mode (0-5)')
    parser.add_argument('-screenblack', action='store_true', help='Set screen black')
    parser.add_argument('-screenwhite', action='store_true', help='Set screen white')
    parser.add_argument('-screenrefresh', action='store_true', help='Refresh screen')
    parser.add_argument('-fullpicturerefresh', action='store_true', help='Full picture refresh')
    parser.add_argument('-partialpicturerefresh', action='store_true', help='Partial picture refresh')
    parser.add_argument('-uploadpicture', help='Upload picture file')
    parser.add_argument('-dither', choices=[m.value for m in DitherMode], default='none', help='Dithering mode')
    parser.add_argument('-setdatetime', type=int, nargs='?', const=0, help='Set datetime with optional hour offset')
    parser.add_argument('-blink', type=int, choices=range(8), help='Set LED color (1=Red,2=Green,3=Blue,4=Yellow,5=Aqua,6=Magenta,7=White,0=Off)')
    parser.add_argument('-gettemp', action='store_true', help='Get temperature')
    parser.add_argument('-getbattery', action='store_true', help='Get battery level')

    args = parser.parse_args()

    epaper = EPaperBLE()

    devices = await EPaperBLE.discover()
    if args.scan:
        print("\nFound devices:")
        for d in devices:
            print(f"Name: {d.name} - Address: {d.address}")
        return

    if args.name:
        matching_devices = [d for d in devices if d.name == args.name]
        if not matching_devices:
            print(f"No device found with name {args.name}")
            return
        args.address = matching_devices[0].address

    if not args.address:
        if not devices:
            print("No EPaper devices found")
            return
        args.address = devices[0].address
        print(f"Using first found device: {devices[0].name} ({args.address})")

    if not await epaper.connect(args.address):
        return

    try:
        if args.setmode is not None:
            await epaper.set_mode(args.setmode)

        if args.screenblack:
            await epaper.screen_black()
 
        if args.screenwhite:
            await epaper.screen_white()

        if args.screenrefresh:
            await epaper.screen_refresh()

        if args.fullpicturerefresh:
            await epaper.full_picture_refresh()

        if args.partialpicturerefresh:
            await epaper.partial_picture_refresh()

        if args.uploadpicture:
            await epaper.upload_picture(args.uploadpicture, DitherMode(args.dither))

        if args.setdatetime is not None:
            await epaper.set_datetime(args.setdatetime)

        if args.blink:
            await epaper.blink(args.blink)

        if args.gettemp:
            temp = await epaper.get_temperature()
            print(f"Temperature: {temp}°C")

        if args.getbattery:
            batt = await epaper.get_battery()
            print(f"Battery: {batt}%")

    finally:
        await epaper.disconnect()

if __name__ == "__main__":
    asyncio.run(main())