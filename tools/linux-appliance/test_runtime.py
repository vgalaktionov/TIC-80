from pathlib import Path
import subprocess
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).parent


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


if __name__ == "__main__":
    unittest.main()
