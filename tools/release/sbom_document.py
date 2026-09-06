# SPDX-License-Identifier: MPL-2.0
"""Deterministic SPDX 2.3 document for distributed files and linked runtimes."""

import datetime
import hashlib
import json


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def sha1(data):
    # SPDX 2.3 mandates SHA1 for package verification codes, not authentication.
    return hashlib.sha1(data, usedforsecurity=False).hexdigest()


def relationship(source, kind, target):
    return {"spdxElementId": source, "relationshipType": kind, "relatedSpdxElement": target}


def runtime_packages(runtime):
    return [{
        "name": component["name"], "SPDXID": "SPDXRef-" + component["id"],
        "downloadLocation": "NOASSERTION", "filesAnalyzed": False,
        "licenseConcluded": component["license"], "licenseDeclared": component["license"],
        "copyrightText": "NOASSERTION",
        "checksums": [{"algorithm": "SHA256", "checksumValue": component["sha256"]}],
        "sourceInfo": f"Input {component['input']} from {runtime['toolchain']}; upstream {component['source_url']}",
        "comment": "Checksum covers the toolchain input, not a separately distributed library. "
                   "Selected members are recorded in RUNTIME-COMPONENTS.json. "
                   "The distribution does not identify the component source commit.",
    } for component in runtime["components"]]


def file_inventory(files, package_id):
    records, relationships, ids = [], [], {}
    for index, (name, data) in enumerate(sorted(files.items())):
        identifier = f"SPDXRef-File-{index}"
        ids[name] = identifier
        records.append({
            "SPDXID": identifier, "fileName": "./" + name,
            "checksums": [{"algorithm": "SHA256", "checksumValue": sha256(data)},
                          {"algorithm": "SHA1", "checksumValue": sha1(data)}],
            "licenseConcluded": "NOASSERTION", "licenseInfoInFiles": ["NOASSERTION"],
            "copyrightText": "NOASSERTION",
        })
        relationships.append(relationship(package_id, "CONTAINS", identifier))
    return records, relationships, ids


def document(payload_name, payload_digest, files):
    metadata = json.loads(files["BUILD-METADATA.json"])
    runtime = json.loads(files["RUNTIME-COMPONENTS.json"])
    commit = metadata["source_commit"]
    epoch = metadata["source_date_epoch"]
    version = metadata["tag"].removeprefix("v").split("-", 1)[0]
    package_id = "SPDXRef-Package-BFS"
    file_records, relationships, ids = file_inventory(files, package_id)
    verification_code = sha1("".join(sorted(sha1(data) for data in files.values())).encode("ascii"))
    packages = [{
        "name": "bfs", "SPDXID": package_id, "versionInfo": version,
        "downloadLocation": "NOASSERTION", "filesAnalyzed": True,
        "packageVerificationCode": {"packageVerificationCodeValue": verification_code},
        "licenseConcluded": "NOASSERTION", "licenseDeclared": "MPL-2.0",
        "licenseInfoFromFiles": ["NOASSERTION"], "copyrightText": "NOASSERTION",
        "checksums": [{"algorithm": "SHA256", "checksumValue": payload_digest}],
        "sourceInfo": f"BFS source commit {commit}; compiler distribution {runtime['toolchain']}",
        "comment": "MPL-2.0 covers BFS source. Linked runtime components carry their own licenses.",
        "externalRefs": [{"referenceCategory": "PACKAGE-MANAGER", "referenceType": "purl",
                          "referenceLocator": f"pkg:github/metaneutrons/BFS@{version}"}],
    }]
    packages.extend(runtime_packages(runtime))
    for name, binary in sorted(runtime["binaries"].items()):
        for item in binary["inputs"]:
            relationships.append(relationship(ids[name], "STATIC_LINK", "SPDXRef-" + item["component"]))
    relationships.insert(0, relationship("SPDXRef-DOCUMENT", "DESCRIBES", package_id))
    return {
        "spdxVersion": "SPDX-2.3", "dataLicense": "CC0-1.0", "SPDXID": "SPDXRef-DOCUMENT",
        "name": payload_name,
        "documentNamespace": f"https://github.com/metaneutrons/BFS/sbom/{commit}/{payload_digest}",
        "creationInfo": {
            "created": datetime.datetime.fromtimestamp(epoch, datetime.UTC).strftime("%Y-%m-%dT%H:%M:%SZ"),
            "creators": ["Organization: metaneutrons", "Tool: bfs-release-sbom/2"],
        },
        "packages": packages, "files": file_records, "relationships": relationships,
        "hasExtractedLicensingInfos": [{
            "licenseId": "LicenseRef-libnix-Public-Domain", "name": "libnix public-domain declaration",
            "extractedText": files["LICENSE.libnix"].decode("utf-8"),
        }],
    }
