#!/usr/bin/env python3
"""Join measured POPS imports to a provider's versioned PSPLibDoc exports."""
import argparse
import hashlib
import json
import xml.etree.ElementTree as ET
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inventory", type=Path)
    parser.add_argument("provider_xml", type=Path)
    parser.add_argument("--out", type=Path, default=Path("out/contract.json"))
    args = parser.parse_args()
    exports = {}
    document = ET.parse(args.provider_xml)
    for module in document.findall(".//PRXFILE"):
        provider = module.findtext("PRX")
        for library in module.findall("./LIBRARIES/LIBRARY"):
            for fn in library.findall("./FUNCTIONS/FUNCTION"):
                exports[(library.findtext("NAME"), int(fn.findtext("NID"), 0))] = {
                    "provider": provider, "export_name": fn.findtext("NAME")}
    inventory = json.loads(args.inventory.read_text())
    matched = []
    for library in inventory["imports"]:
        for fn in library["functions"]:
            key = library["name"], fn["nid"]
            if key in exports:
                matched.append({"library": key[0], "nid": f"0x{key[1]:08X}",
                                "stub": f"0x{fn['stub']:08X}", **exports[key]})
    report = {
        "pops_sha256": inventory["input"]["sha256"],
        "provider_xml_sha256": hashlib.sha256(args.provider_xml.read_bytes()).hexdigest(),
        "attribution": "Matched library and NID against metadata; not a live loader trace",
        "matched_import_count": len(matched), "matched_imports": matched}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    counts = {}
    for fn in matched:
        counts[fn["library"]] = counts.get(fn["library"], 0) + 1
    print(json.dumps({"matched": len(matched), "by_library": counts}, indent=2))


if __name__ == "__main__":
    main()
