"""Generate the private event-controller firmware header for one PC."""

import ipaddress
import os
from pathlib import Path
import sys

Import("env")

project = Path(env.subst("$PROJECT_DIR"))
sys.path.insert(0, str(project / "scripts"))
from wifi_config import WifiConfigError, load_config, cpp_string

try:
    config_file = Path(os.environ.get(
        "KJ_WIFI_CONFIG_FILE", str(project.parent.parent / ".env")
    )).expanduser().resolve()
    wifi = load_config(config_file)
    host = ipaddress.IPv4Address(os.environ["KJ_CONTROLLER_HOST_IP"])
    color = os.environ["KJ_CONTROLLER_COLOR"].removeprefix("#")
    if len(color) != 6 or any(character not in "0123456789abcdefABCDEF" for character in color):
        raise ValueError("KJ_CONTROLLER_COLOR must be a six-digit RGB hex color")
except (KeyError, ValueError, WifiConfigError) as error:
    print(f"Event controller configuration: {error}", file=sys.stderr)
    env.Exit(1)

generated = Path(env.subst("$BUILD_DIR")) / "generated"
generated.mkdir(parents=True, exist_ok=True)
header = generated / "kj_event_config.h"
contents = (
    "// Generated locally; do not commit.\n#pragma once\n"
    f"const char* WIFI_SSID = {cpp_string(wifi['ssid'])};\n"
    f"const char* WIFI_PASS = {cpp_string(wifi['password'])};\n"
    f"constexpr uint8_t KJ_HOST_IP[4] = {{{','.join(str(part) for part in host.packed)}}};\n"
    f"constexpr uint8_t KJ_LED_RGB[3] = {{{','.join(str(int(color[i:i+2], 16)) for i in (0, 2, 4))}}};\n"
)
if not header.exists() or header.read_text(encoding="utf-8") != contents:
    header.write_text(contents, encoding="utf-8")
header.chmod(0o600)
env.Append(CPPPATH=[str(generated)], CPPDEFINES=["KJ_EVENT_CONFIG"])
