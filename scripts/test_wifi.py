"""PlatformIO PRE script for the two experiment profiles only."""

import os
import sys
from pathlib import Path

Import("env")

project = Path(env.subst("$PROJECT_DIR"))
sys.path.insert(0, str(project / "scripts"))
from wifi_config import WifiConfigError, load_config, write_header

config_path = Path(os.environ.get(
    "KJ_WIFI_CONFIG_FILE", str(project.parent.parent / ".env")
)).expanduser().resolve()
generated = Path(env.subst("$BUILD_DIR")) / "generated"
try:
    write_header(load_config(config_path), generated / "kj_test_wifi.h")
except WifiConfigError as error:
    print(f"Controller AP: {error}", file=sys.stderr)
    env.Exit(1)

env.Append(CPPPATH=[str(generated)], CPPDEFINES=["KJ_TEST_WIFI_CONFIG"])
