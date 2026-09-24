import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

from run_elf_batch import write_junit
from send_elf import READY_BOARD_RE, READY_RE


class UartStreamProtocolTests(unittest.TestCase):
    def test_ready_record_reports_bpif3_identity(self):
        line = (
            b"[UART_STREAM] READY version=1 header_bytes=88 "
            b"max_elf_bytes=134217728 buffer=0x000000000c200000 "
            b"board=bpif3_k1 runner_build=bpif3-uart-poc-v1\n"
        )
        match = READY_RE.search(line)
        self.assertIsNotNone(match)
        self.assertEqual(match.groups(), (b"1", b"134217728", b"bpif3-uart-poc-v1"))
        self.assertEqual(READY_BOARD_RE.search(line).group(1), b"bpif3_k1")

    def test_ready_record_remains_compatible_with_first_vf2_image(self):
        line = (
            b"[UART_STREAM] READY version=1 header_bytes=88 "
            b"max_elf_bytes=134217728 buffer=0x0000000088000000 "
            b"runner_build=98af2bd5b187\n"
        )
        match = READY_RE.search(line)
        self.assertIsNotNone(match)
        self.assertEqual(match.groups(), (b"1", b"134217728", b"98af2bd5b187"))
        self.assertIsNone(READY_BOARD_RE.search(line))

    def test_junit_uses_selected_board_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "junit.xml"
            write_junit(
                output,
                [
                    {
                        "name": "ExceptionsF-00",
                        "status": "PASS",
                        "elapsed_seconds": 1.25,
                        "uart_log": "case.uart.log",
                    }
                ],
                1.25,
                "bpif3_k1",
            )
            root = ET.parse(output).getroot()

        self.assertEqual(root.attrib["name"], "bpif3_k1-uart-stream")
        self.assertEqual(root.find("testcase").attrib["classname"], "bpif3_k1.uart_stream")


if __name__ == "__main__":
    unittest.main()
