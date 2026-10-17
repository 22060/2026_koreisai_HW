"""Generate the default firmware Wi-Fi header from the repository root .env."""

import os
import sys
from pathlib import Path

Import("env")

project = Path(env.subst("$PROJECT_DIR"))
root = project.parent.parent
sys.path.insert(0, str(project / "scripts"))
from wifi_config import WifiConfigError, load_config, write_header

generated = Path(env.subst("$BUILD_DIR")) / "generated"
try:
    config_path = Path(os.environ.get("KJ_WIFI_CONFIG_FILE", str(root / ".env"))).expanduser().resolve()
    config_index = os.environ.get("KJ_WIFI_CONFIG_INDEX", "1")
    write_header(load_config(config_path, config_index), generated / "kj_wifi_config.h")
except WifiConfigError as error:
    print(f"Wi-Fi configuration: {error}", file=sys.stderr)
    env.Exit(1)

env.Append(CPPPATH=[str(generated)], CPPDEFINES=["KJ_ENV_WIFI_CONFIG"])
