#!/usr/bin/env python3
"""Write a CycloneDX 1.5 SBOM (JSON) for a built Plane Radar firmware.

Run after `pio run -e s3`; it reads the versions PlatformIO actually
installed, not the ones platformio.ini asks for:

    python scripts/make_sbom.py --version v1.2.3 --out dist/plane-radar-v1.2.3.cdx.json

Covers: the pioarduino platform, the Arduino-ESP32 core, the precompiled
ESP-IDF and the components linked into it (from its dependencies.lock),
mbedTLS and lwIP versions from the IDF headers, the project libraries in
.pio/libdeps, and the bundled data (airport list, font).
"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import re
import sys
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE_DIR = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
CHIP = "esp32s3"

# Licences not declared in the libraries' library.json files.
KNOWN_LICENSES = {
    "ArduinoJson": "MIT",
    "WiFiManager": "MIT",
    "framework-arduinoespressif32": "LGPL-2.1-or-later",
    "esp-idf": "Apache-2.0",
    "mbedtls": "Apache-2.0",
    "lwip": "BSD-3-Clause",
}
TEST_ONLY_LIBS = {"Unity"}


def read_json(path: Path) -> dict:
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def component(name, version, ctype="library", *, purl=None, url=None,
              license_id=None, description=None, scope="required", hashes=None):
    c = {"type": ctype, "name": name, "version": str(version), "scope": scope,
         "bom-ref": f"{name}@{version}"}
    if description:
        c["description"] = description
    lic = license_id or KNOWN_LICENSES.get(name)
    if lic == "public-domain":
        c["licenses"] = [{"license": {"name": "Public domain"}}]
    elif lic and re.fullmatch(r"[A-Za-z0-9.\-+]+", lic):
        c["licenses"] = [{"license": {"id": lic}}]
    elif lic:
        c["licenses"] = [{"expression": lic}]
    if purl:
        c["purl"] = purl
    if url:
        c["externalReferences"] = [{"type": "vcs", "url": url}]
    if hashes:
        c["hashes"] = hashes
    return c


def sha256_of(path: Path) -> list[dict]:
    return [{"alg": "SHA-256",
             "content": hashlib.sha256(path.read_bytes()).hexdigest()}]


def define(header: Path, name: str) -> str | None:
    m = re.search(rf"#define\s+{name}\s+\"?([^\"\s]+)", header.read_text(errors="ignore"))
    return m.group(1) if m else None


def find_one(base: Path, pattern: str) -> Path | None:
    return next(iter(sorted(base.rglob(pattern))), None)


def idf_lock_components(lock: Path) -> list[dict]:
    """Minimal reader for dependencies.lock (YAML, fixed layout)."""
    out = []
    name = None
    for line in lock.read_text().splitlines():
        top = re.match(r"^  ([^\s:][^:]*):\s*$", line)
        if top:
            name = top.group(1)
            continue
        ver = re.match(r"^    version:\s*'?([^'\s]+)'?\s*$", line)
        if ver and name and name != "idf":
            ns, _, short = name.rpartition("/")
            out.append(component(
                name, ver.group(1),
                purl=f"pkg:generic/{ns or 'espressif'}/{short}@{ver.group(1)}",
                scope="optional",
                description="ESP-IDF managed component shipped with the precompiled "
                            "core; in the image only if something uses it"))
            name = None
    return out


def build(version: str, env: str) -> dict:
    comps: list[dict] = []
    packages = CORE_DIR / "packages"

    platform = read_json(CORE_DIR / "platforms" / "espressif32" / "platform.json")
    comps.append(component("platform-espressif32 (pioarduino)", platform["version"],
                           "platform", url="https://github.com/pioarduino/platform-espressif32",
                           license_id="Apache-2.0", scope="excluded",
                           description="Build platform (not in the image)"))

    core = read_json(packages / "framework-arduinoespressif32" / "package.json")
    comps.append(component("framework-arduinoespressif32", core["version"], "framework",
                           purl=f"pkg:github/espressif/arduino-esp32@{core['version']}",
                           url="https://github.com/espressif/arduino-esp32"))

    libs_dir = packages / "framework-arduinoespressif32-libs"
    idf_version = None
    versions_txt = libs_dir / "versions.txt"
    if versions_txt.exists():
        m = re.search(r"^esp-idf:\s*v?(\S+)\s*(\S*)", versions_txt.read_text(), re.M)
        if m:
            idf_version = m.group(1)
            idf_commit = m.group(2)
    if idf_version:
        c = component("esp-idf", idf_version, "framework",
                      purl=f"pkg:github/espressif/esp-idf@v{idf_version}",
                      url="https://github.com/espressif/esp-idf",
                      description=f"Precompiled for {CHIP} (commit {idf_commit})")
        comps.append(c)

    chip_dir = libs_dir / CHIP
    mbed = find_one(chip_dir, "build_info.h")
    if mbed and (v := define(mbed, "MBEDTLS_VERSION_STRING")):
        comps.append(component("mbedtls", v, purl=f"pkg:github/Mbed-TLS/mbedtls@v{v}",
                               description="TLS and the OTA signature check (part of ESP-IDF)"))
    lwip = next((p for p in chip_dir.rglob("init.h") if p.parent.name == "lwip"), None)
    if lwip:
        parts = [define(lwip, f"LWIP_VERSION_{k}") for k in ("MAJOR", "MINOR", "REVISION")]
        if all(parts):
            v = ".".join(parts)
            comps.append(component("lwip", v, description="TCP/IP stack (part of ESP-IDF)"))
    lock = chip_dir / "dependencies.lock"
    if lock.exists():
        comps.extend(idf_lock_components(lock))

    libdeps = ROOT / ".pio" / "libdeps" / env
    for lib_json in sorted(libdeps.glob("*/library.json")):
        d = read_json(lib_json)
        name = d["name"]
        repo = (d.get("repository") or {}).get("url")
        comps.append(component(
            name, d["version"], license_id=d.get("license") or None, url=repo,
            purl=f"pkg:platformio/{name}@{d['version']}",
            scope="excluded" if name in TEST_ONLY_LIBS else "required"))

    font = ROOT / "data" / "ui_font.vlw"
    if font.exists():
        comps.append(component("Noto Sans Bold 15 (VLW)", "1", "data",
                               license_id="OFL-1.1", hashes=sha256_of(font),
                               description="UI font, embedded"))
    airports = ROOT / "src" / "data" / "large_airports_data.cpp"
    if airports.exists():
        comps.append(component("OurAirports large-airport extract", "bundled", "data",
                               license_id="public-domain", hashes=sha256_of(airports),
                               url="https://github.com/davidmegginson/ourairports-data",
                               description="Runway outlines (public domain)"))

    image = ROOT / ".pio" / "build" / env / "firmware.bin"
    main = component("plane-radar", version, "firmware",
                     hashes=sha256_of(image) if image.exists() else None)
    main.pop("scope")

    return {
        "bomFormat": "CycloneDX",
        "specVersion": "1.5",
        "serialNumber": f"urn:uuid:{uuid.uuid4()}",
        "version": 1,
        "metadata": {
            "timestamp": dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat(),
            "tools": {"components": [{"type": "application", "name": "scripts/make_sbom.py"}]},
            "component": main,
        },
        "components": comps,
        "dependencies": [{"ref": main["bom-ref"],
                          "dependsOn": [c["bom-ref"] for c in comps if c["scope"] == "required"]}],
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--version", default=os.environ.get("PLANE_RADAR_VERSION", "dev"))
    ap.add_argument("--env", default="s3")
    ap.add_argument("--out", default="-")
    args = ap.parse_args()
    bom = build(args.version, args.env)
    text = json.dumps(bom, indent=2) + "\n"
    if args.out == "-":
        sys.stdout.write(text)
    else:
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.out).write_text(text)
        print(f"SBOM: {args.out} ({len(bom['components'])} components)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
