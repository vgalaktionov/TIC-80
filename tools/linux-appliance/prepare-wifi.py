"""Prepare a private NetworkManager keyfile without logging credentials."""

import argparse
import configparser
import os
from pathlib import Path
import re


def read_wpa(path):
    # Deliberately accept only one network and plain quoted or hex values.
    # Do not guess how unsupported wpa_supplicant escape sequences were intended.
    text = Path(path).read_text()
    networks = re.findall(r"^\s*network\s*=\s*\{(.*?)^\s*\}", text, re.M | re.S)
    if len(networks) != 1:
        raise ValueError("Wi-Fi import requires exactly one network block")
    values = {}
    for name in ("ssid", "psk"):
        matches = re.findall(r"^\s*" + name + r"\s*=\s*(.*?)\s*$", networks[0], re.M)
        if len(matches) != 1:
            raise ValueError("Wi-Fi import requires one ssid and one psk")
        value = matches[0]
        quoted = re.fullmatch(r'"([^"\\]*)"\s*(?:#.*)?', value)
        if quoted:
            values[name] = quoted[1]
        elif re.fullmatch(r"[0-9a-fA-F]+", value):
            values[name] = bytes.fromhex(value).decode("utf-8") if name == "ssid" else value
        else:
            raise ValueError("Unsupported Wi-Fi value syntax; use explicit Wi-Fi arguments")
    return values["ssid"], values["psk"]


def keyfile(ssid, password):
    if not 1 <= len(ssid.encode("utf-8")) <= 32 or any(c in ssid for c in "\0\r\n"):
        raise ValueError("SSID must contain 1-32 UTF-8 bytes without line breaks")
    if not (re.fullmatch(r"[0-9a-fA-F]{64}", password)
            or (8 <= len(password) <= 63 and all(32 <= ord(c) <= 126 for c in password))):
        raise ValueError("WPA password must be 8-63 printable ASCII characters or a 64-digit hex PSK")

    def escape(value):
        return value.replace("\\", "\\\\").replace(" ", "\\s").replace("\t", "\\t")

    config = configparser.ConfigParser(interpolation=None)
    config["connection"] = {"id": "tic80-wifi", "type": "wifi", "autoconnect": "true"}
    config["wifi"] = {"mode": "infrastructure", "ssid": escape(ssid)}
    config["wifi-security"] = {"key-mgmt": "wpa-psk", "psk": escape(password)}
    config["ipv4"] = {"method": "auto"}
    config["ipv6"] = {"method": "auto"}
    return config


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--wifi-ssid")
    parser.add_argument("--wifi-password")
    parser.add_argument("--wifi-from-wpa", type=Path)
    args = parser.parse_args()
    try:
        if args.wifi_from_wpa:
            if args.wifi_ssid is not None or args.wifi_password is not None:
                raise ValueError("Choose Wi-Fi arguments or --wifi-from-wpa, not both")
            ssid, password = read_wpa(args.wifi_from_wpa)
        elif args.wifi_ssid is not None or args.wifi_password is not None:
            if args.wifi_ssid is None or args.wifi_password is None:
                raise ValueError("Both --wifi-ssid and --wifi-password are required")
            ssid, password = args.wifi_ssid, args.wifi_password
        else:
            return
        config = keyfile(ssid, password)
        args.output.mkdir(mode=0o700, parents=True, exist_ok=True)
        target = args.output / "tic80-wifi.nmconnection"
        fd = os.open(target, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "w") as stream:
            config.write(stream, space_around_delimiters=False)
    except (ValueError, OSError):
        # Parser and OS exceptions can contain parts of the secret input.
        parser.exit(2, "Invalid or unreadable Wi-Fi configuration; check arguments and supported format.\n")


if __name__ == "__main__":
    main()
