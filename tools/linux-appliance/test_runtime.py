from pathlib import Path
import subprocess
import runpy
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).parent
WIFI = runpy.run_path(str(ROOT / "prepare-wifi.py"))


class ConfigTests(unittest.TestCase):
    def test_shell_syntax(self):
        scripts = [ROOT / name for name in ("build.sh", "config", "runtime/profile")]
        scripts += list((ROOT / "stage-tic80").rglob("*.sh"))
        scripts += [ROOT / "runtime" / name for name in ("launch", "session", "import-wifi")]
        for script in scripts:
            subprocess.run(["bash", "-n", str(script)], check=True)

    def test_openbox_xml(self):
        ET.parse(ROOT / "runtime/openbox.xml")

    def test_ssh_is_key_only(self):
        config = dict(line.split(None, 1) for line in (ROOT / "runtime/sshd.conf").read_text().splitlines())
        self.assertEqual(config["PubkeyAuthentication"], "yes")
        self.assertEqual(config["PasswordAuthentication"], "no")
        self.assertEqual(config["KbdInteractiveAuthentication"], "no")
        self.assertEqual(config["PermitRootLogin"], "no")
        self.assertEqual(config["AllowUsers"], "tic80")

    def test_no_http_logger(self):
        self.assertFalse((ROOT / "runtime/log-server.py").exists())
        self.assertFalse((ROOT / "runtime/tic80-logs.service").exists())

    def test_wifi_keyfile_escaping(self):
        config = WIFI["keyfile"](" test;#%\\wifi ", " example%#\\password ")
        self.assertEqual(config["wifi"]["ssid"], r"\stest;#%\\wifi\s")
        self.assertEqual(config["wifi-security"]["psk"], r"\sexample%#\\password\s")

    def test_wifi_validation(self):
        for ssid, password in [("", "password"), ("x" * 33, "password"),
                               ("wifi\nother", "password"), ("wifi", "short")]:
            with self.assertRaises(ValueError):
                WIFI["keyfile"](ssid, password)
        WIFI["keyfile"]("wifi", "a" * 64)

    def test_wpa_import(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "wpa.conf"
            path.write_text('country=NL\nnetwork={\n ssid="test wifi"\n psk="test password"\n}\n')
            self.assertEqual(WIFI["read_wpa"](path), ("test wifi", "test password"))
            path.write_text(path.read_text() + path.read_text())
            with self.assertRaises(ValueError):
                WIFI["read_wpa"](path)

    def test_private_wifi_file(self):
        with tempfile.TemporaryDirectory() as folder:
            result = subprocess.run(["python3", str(ROOT / "prepare-wifi.py"),
                                     "--output", folder, "--wifi-ssid", "example",
                                     "--wifi-password", "test-password"], capture_output=True, check=True)
            self.assertEqual(result.stdout + result.stderr, b"")
            path = Path(folder) / "tic80-wifi.nmconnection"
            self.assertEqual(path.stat().st_mode & 0o777, 0o600)



if __name__ == "__main__":
    unittest.main()
