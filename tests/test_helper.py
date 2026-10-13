import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("configure_mail", Path(__file__).parents[1] / "scripts/configure_mail.py")
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)


class SerialFake:
    def __init__(self, **kwargs):
        self.kwargs = kwargs
        self.writes = []
        self.in_waiting = 0
        self.chunks = []

    def read(self, n):
        return self.chunks.pop(0) if self.chunks else b""

    def write(self, data):
        self.writes.append(data)

    def flush(self):
        pass

    def open(self):
        assert self.dtr is False and self.rts is False
        assert self.port == "COM_TEST"


class HelperTests(unittest.TestCase):
    def test_colored_mail_result(self):
        data = b'\x1b[0;32mI (123) service: MAIL_RESULT=DATA_ACCEPTED\x1b[0m\r\n'
        self.assertEqual(helper.mail_result_value(data), 'DATA_ACCEPTED')

    def test_incomplete_mail_result_is_not_accepted(self):
        self.assertIsNone(helper.mail_result_value(b'MAIL_RESULT=DATA_ACCEPT'))

    def test_open_does_not_toggle_reset(self):
        port = helper.connect("COM_TEST", factory=SerialFake)
        self.assertEqual(port.kwargs["baudrate"], 115200)
        self.assertIsNone(port.kwargs["port"])

    def test_fragmented_prompt(self):
        port = SerialFake()
        port.chunks = [b"INPUT_", b"AUTHORIZATION_", b"HIDDEN\r\n"]
        self.assertIn(b"INPUT_AUTHORIZATION_HIDDEN", helper.wait_for(port, "INPUT_AUTHORIZATION_HIDDEN", 1))

    def test_reject_console_injection(self):
        port = SerialFake()
        with self.assertRaises(ValueError):
            helper.line(port, "secret\rmail_send_test")
        self.assertFalse(port.writes)

    def test_timeout_does_not_disclose_device_bytes(self):
        port = SerialFake()
        port.chunks = [b"example-secret"]
        with self.assertRaises(TimeoutError) as err:
            helper.wait_for(port, "missing", 0.01)
        self.assertNotIn("example-secret", str(err.exception))


if __name__ == "__main__":
    unittest.main()
