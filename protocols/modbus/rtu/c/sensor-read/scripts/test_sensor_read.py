"""Integration tests for the standalone C CLI, using a simulated Modbus serial device."""
import os
from pathlib import Path
import pty
import re
import select
import signal
import struct
import subprocess
import termios
import threading
import time
import unittest

ROOT=Path(__file__).resolve().parents[1]
BINARY=Path(os.environ.get("SENSOR_READ_BINARY", str(ROOT/"sensor_read_pc")))


def crc(data):
    value=0xffff
    for byte in data:
        value^=byte
        for _ in range(8):
            value=(value>>1)^0xa001 if value&1 else value>>1
    return struct.pack("<H",value)


class SensorRead(unittest.TestCase):
    def run_sensor(self,args=(),values=(278,),mode="normal",count=1):
        master,slave=pty.openpty()
        saved=termios.tcgetattr(slave)
        requests=[]
        errors=[]
        def sensor():
            try:
                for index in range(count):
                    request=b""
                    while len(request)<8:
                        ready,_,_=select.select([master],[],[],3)
                        if not ready:
                            raise AssertionError("No complete Modbus request")
                        request+=os.read(master,8-len(request))
                    self.assertEqual(request[6:],crc(request[:6]))
                    requests.append(request)
                    if mode=="timeout" or (mode=="good_then_timeout" and index==1):
                        continue
                    if mode=="exception":
                        data=bytes([request[0],request[1]|0x80,2])
                    else:
                        data=bytes([request[0],request[1],len(values)*2])+b"".join(struct.pack(">H",v) for v in values)
                    response=data+crc(data)
                    if mode=="crc":
                        response=response[:-1]+bytes([response[-1]^0xff])
                    if mode=="wrong_unit":
                        data=bytes([2])+data[1:]
                        response=data+crc(data)
                    if mode=="echo":
                        os.write(master,request)
                    if mode=="fragment":
                        os.write(master,response[:3])
                        time.sleep(.01)
                        os.write(master,response[3:])
                    else:
                        os.write(master,response)
            except Exception as error:
                errors.append(error)
        worker=threading.Thread(target=sensor)
        worker.start()
        try:
            result=subprocess.run([str(BINARY),"-d",os.ttyname(slave),"-t","150","-i","50",*args],
                                  capture_output=True,text=True,timeout=5)
            worker.join(4)
            self.assertFalse(worker.is_alive())
            self.assertEqual(errors,[])
            self.assertEqual(termios.tcgetattr(slave),saved)
            self.assertFalse(re.search(r"[\u4e00-\u9fff]",result.stdout+result.stderr))
            return result,requests
        finally:
            os.close(master);os.close(slave)

    def test_default_unsigned(self):
        r,requests=self.run_sensor(values=(65535,))
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertIn("R0: 65535",r.stdout)
        self.assertEqual(requests[0][:6],bytes.fromhex("01 03 00 00 00 01"))

    def test_negative_temperature_and_label(self):
        r,_=self.run_sensor(["--type","i16","--scale","0.1","--name","Temperature","--unit","C","--raw"],(65483,))
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertIn("Temperature (R0): -5.3 C",r.stdout)
        self.assertIn("R0=0xffcb(65483)",r.stdout)

    def test_sign_magnitude(self):
        r,_=self.run_sensor(["--type","signmag16","--scale","0.1"],(0x8035,))
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertIn("R0: -5.3",r.stdout)

    def test_float_byte_orders(self):
        packed=struct.pack(">f",27.75)
        for order in ("ABCD","BADC","CDAB","DCBA"):
            with self.subTest(order=order):
                values=struct.unpack(">HH",bytes(packed["ABCD".index(x)] for x in order))
                r,_=self.run_sensor(["-q","2","--type","f32","--order",order],values)
                self.assertEqual(r.returncode,0,r.stderr)
                self.assertIn("R0: 27.75",r.stdout)

    def test_32bit_signed_unsigned(self):
        for kind,expected in (("i32","-2147483648"),("u32","2147483648")):
            r,_=self.run_sensor(["-q","2","--type",kind],(0x8000,0))
            self.assertEqual(r.returncode,0,r.stderr)
            self.assertIn("R0: "+expected,r.stdout)

    def test_multiple_values_fc04_hex_address(self):
        r,requests=self.run_sensor(["-a","7","-f","4","-s","0x0010","-q","2","--scale","0.1"],(278,600))
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertIn("R16: 27.8",r.stdout)
        self.assertIn("R17: 60",r.stdout)
        self.assertEqual(requests[0][:6],bytes.fromhex("07 04 00 10 00 02"))

    def test_register_pairs(self):
        r,_=self.run_sensor(["-q","4","--type","u32"],(0,1,0,2))
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertIn("R0: 1",r.stdout)
        self.assertIn("R2: 2",r.stdout)
        self.assertNotIn("R1:",r.stdout)

    def test_max_quantity(self):
        r,_=self.run_sensor(["-q","16"],tuple(range(16)))
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertIn("R15: 15",r.stdout)

    def test_scale_offset_precision(self):
        r,_=self.run_sensor(["--scale","-0.1","--offset","5","--precision","2"],(20,))
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertIn("R0: 3.00",r.stdout)

    def test_fragment_and_echo(self):
        for mode in ("fragment","echo"):
            with self.subTest(mode=mode):
                r,_=self.run_sensor(mode=mode)
                self.assertEqual(r.returncode,0,r.stderr)
                self.assertIn("R0: 278",r.stdout)

    def test_errors_do_not_print_values(self):
        for mode in ("timeout","crc","wrong_unit","exception"):
            with self.subTest(mode=mode):
                r,_=self.run_sensor(mode=mode)
                self.assertEqual(r.returncode,1,r.stderr)
                self.assertNotIn("R0:",r.stdout)
                self.assertIn("failed=1",r.stdout)

    def test_no_stale_value_after_timeout(self):
        r,_=self.run_sensor(["-n","2"],mode="good_then_timeout",count=2)
        self.assertEqual(r.returncode,1)
        self.assertEqual(r.stdout.count("R0:"),1)
        self.assertIn("successful=1, failed=1",r.stdout)

    def test_nan_is_decode_failure(self):
        r,_=self.run_sensor(["-q","2","--type","f32"],(0x7fc0,0))
        self.assertEqual(r.returncode,1)
        self.assertNotIn("R0:",r.stdout)
        self.assertIn("non-finite",r.stderr)

    def test_invalid_args_before_open(self):
        cases=[["-a","0"],["-f","6"],["-q","17"],["-q","1","--type","f32"],
               ["-s","65535","-q","2"],["--type","unknown"],["--order","AABC"],
               ["--type","u16","--order","CDAB"],["--scale","nan"],["--offset","inf"],
               ["-p","X"],["--precision","7"]]
        for args in cases:
            with self.subTest(args=args):
                r=subprocess.run([str(BINARY),"-d","/does/not/exist",*args],capture_output=True,text=True)
                self.assertEqual(r.returncode,2)
                self.assertNotIn("open serial",r.stderr)

    def test_interrupt_restores_serial(self):
        master,slave=pty.openpty()
        saved=termios.tcgetattr(slave)
        process=subprocess.Popen([str(BINARY),"-d",os.ttyname(slave),"-n","0"],
                                 stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            ready,_,_=select.select([master],[],[],3)
            self.assertTrue(ready)
            os.read(master,8)
            process.send_signal(signal.SIGINT)
            out,err=process.communicate(timeout=3)
            self.assertEqual(process.returncode,130,(out,err))
            self.assertEqual(termios.tcgetattr(slave),saved)
        finally:
            if process.poll() is None:
                process.kill();process.wait()
            os.close(master);os.close(slave)


if __name__=="__main__":
    unittest.main()

