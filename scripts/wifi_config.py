"""Read private Wi-Fi configuration and encode bytes safely for the ESP firmware."""

import json
import re
from pathlib import Path


class WifiConfigError(ValueError):
    pass


def _config_index(index):
    try:
        index = int(index)
    except (TypeError, ValueError):
        raise WifiConfigError("コントローラ番号は1〜3で指定してください。") from None
    if index not in (1, 2, 3):
        raise WifiConfigError("コントローラ番号は1〜3で指定してください。")
    return index


def validate_config(config):
    if not isinstance(config, dict):
        raise WifiConfigError("AP設定はJSONオブジェクトで指定してください。")
    ssid = config.get("ssid")
    password = config.get("password")
    try:
        ssid_length = len(ssid.encode("utf-8")) if isinstance(ssid, str) else 0
    except UnicodeEncodeError:
        raise WifiConfigError("SSIDは有効なUTF-8文字列にしてください。") from None
    if not 1 <= ssid_length <= 32:
        raise WifiConfigError("SSIDはUTF-8で1〜32バイトにしてください。")
    if any(ord(char) < 32 or ord(char) == 127 for char in ssid):
        raise WifiConfigError("SSIDに改行・制御文字は使えません。")
    if not isinstance(password, str) or not 8 <= len(password) <= 63:
        raise WifiConfigError("Wi-Fiパスワードは半角8〜63文字にしてください。")
    if any(not 32 <= ord(char) <= 126 for char in password):
        raise WifiConfigError("Wi-Fiパスワードには半角の表示可能な文字を使ってください。")
    return {"ssid": ssid, "password": password}


def load_config(path, index=1):
    index = _config_index(index)
    path = Path(path)
    if path.name == ".env":
        return load_env_config(path, index)
    try:
        config = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise WifiConfigError("Wi-Fi設定ファイルがありません。ルートの .env に WIFI_SSID と WIFI_PASS を設定してください。") from None
    except (OSError, ValueError, UnicodeError):
        # Do not include the document or JSON decoder's excerpt in error output.
        raise WifiConfigError("AP設定ファイルを読み込めません。JSON形式・権限を確認してください。") from None
    return validate_config(config)


def load_env_config(path, index=1):
    """Read one numbered Wi-Fi profile from a root .env file."""
    index = _config_index(index)
    values = {number: {} for number in (1, 2, 3)}
    try:
        lines = Path(path).read_text(encoding="utf-8").splitlines()
    except FileNotFoundError:
        raise WifiConfigError("Wi-Fi設定がありません。ルートの .env に WIFI_SSID1 / WIFI_PASS1 などを設定してください。") from None
    except (OSError, UnicodeError):
        raise WifiConfigError("Wi-Fi設定ファイルを読み込めません。形式・権限を確認してください。") from None

    for line in lines:
        entry = line.strip()
        if not entry or entry.startswith("#"):
            continue
        if entry.startswith("export "):
            entry = entry[7:].lstrip()
        match = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)", entry)
        if not match:
            continue
        name, raw_value = match.groups()
        key = re.fullmatch(r"WIFI_(SSID|PASS)_?([1-3])?", name)
        if not key:
            continue
        field, number = key.groups()
        number = int(number) if number else 1
        field = "ssid" if field == "SSID" else "password"
        if field in values[number]:
            raise WifiConfigError(".env に同じWi-Fi設定が複数あります。")
        raw_value = raw_value.strip()
        if raw_value.startswith('"'):
            try:
                value, end = json.JSONDecoder().raw_decode(raw_value)
                trailing = raw_value[end:].strip()
                if not isinstance(value, str) or (trailing and not trailing.startswith("#")):
                    raise ValueError
            except ValueError:
                raise WifiConfigError(".env のWi-Fi設定値を読み込めません。") from None
        elif raw_value.startswith("'"):
            if len(raw_value) < 2 or not raw_value.endswith("'"):
                raise WifiConfigError(".env のWi-Fi設定値を読み込めません。")
            value = raw_value[1:-1]
        else:
            value = re.split(r"\s+#", raw_value, maxsplit=1)[0].strip()
        values[number][field] = value

    for number, config in values.items():
        # Empty pairs are useful as placeholders for unused profiles.
        if not any(config.values()):
            continue
        if not config.get("ssid") or not config.get("password"):
            raise WifiConfigError(
                f".env に WIFI_SSID{number} と WIFI_PASS{number} を両方設定してください。"
            )
        validate_config(config)

    selected = values[index]
    if not selected.get("ssid") or not selected.get("password"):
        raise WifiConfigError(
            f"コントローラ{index}のWi-Fi設定がありません。.env に "
            f"WIFI_SSID{index} と WIFI_PASS{index} を設定してください。"
        )
    return validate_config(selected)


def cpp_string(value):
    # Fixed-width octal escapes preserve UTF-8 and avoid quotes, shell syntax and
    # adjacent hexadecimal digits changing the meaning of a C++ string literal.
    return '"' + "".join(f"\\{byte:03o}" for byte in value.encode("utf-8")) + '"'


def write_header(config, path):
    config = validate_config(config)
    path = Path(path)
    contents = (
        "// Generated locally; do not commit.\n#pragma once\n"
        f"const char* WIFI_SSID = {cpp_string(config['ssid'])};\n"
        f"const char* WIFI_PASS = {cpp_string(config['password'])};\n"
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding="utf-8") != contents:
        path.write_text(contents, encoding="utf-8")
    path.chmod(0o600)
