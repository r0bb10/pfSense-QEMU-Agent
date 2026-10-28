#!/usr/bin/env python3
"""Stage pfSense userspace development files; never install packages on the host."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request


def digest(path):
    with path.open("rb") as handle:
        return hashlib.file_digest(handle, "sha256").hexdigest()


def extract_payload(archive, destination, prefixes):
    """Extract only development/runtime files and keep links inside the sysroot."""
    hardlinks = []
    for member in archive:
        name = PurePosixPath(member.name.lstrip("/"))
        if ".." in name.parts:
            raise RuntimeError("Unsafe package path: " + member.name)
        if not any(name == prefix or prefix in name.parents for prefix in prefixes):
            continue
        target = destination / str(name)
        target.parent.mkdir(parents=True, exist_ok=True)
        if member.isdir():
            target.mkdir(exist_ok=True)
        elif member.issym():
            link = member.linkname
            resolved = (destination / link.lstrip("/")) if link.startswith("/") else target.parent / link
            resolved = Path(os.path.abspath(resolved))
            if not resolved.is_relative_to(destination):
                raise RuntimeError("Unsafe package link: " + member.name)
            if target.is_symlink() or target.exists():
                target.unlink()
            target.symlink_to(os.path.relpath(resolved, target.parent))
        elif member.islnk():
            hardlinks.append((target, member.linkname))
        elif member.isfile():
            # A prior package symlink must not redirect writes outside staging.
            if target.is_symlink():
                target.unlink()
            with archive.extractfile(member) as source, target.open("wb") as output:
                shutil.copyfileobj(source, output)
            target.chmod(member.mode & 0o777)
    for target, link in hardlinks:
        source = destination / link.lstrip("/")
        if not source.resolve().is_relative_to(destination):
            raise RuntimeError("Unsafe package hard link: " + link)
        shutil.copyfile(source, target)


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, default=root / "work" / "sysroot-downloads")
    parser.add_argument("--work", type=Path, default=root / "work")
    args = parser.parse_args()
    manifest = json.loads((root / "scripts" / "pfsense-2.8.1-sysroot.json").read_text())
    args.cache.mkdir(parents=True, exist_ok=True)
    args.work.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix="pfsense-sdk-", dir=args.work)).resolve()
    sysroot = directory / "sysroot"
    sysroot.mkdir()
    for package in manifest["packages"]:
        cached = args.cache / package["name"]
        if not cached.exists():
            print("Downloading " + package["name"], file=sys.stderr)
            temporary = cached.with_name(cached.name + ".part")
            try:
                with urllib.request.urlopen(package["url"], timeout=120) as response, temporary.open("wb") as output:
                    shutil.copyfileobj(response, output)
                if digest(temporary) != package["sha256"]:
                    raise RuntimeError("SHA-256 mismatch: " + package["name"])
                temporary.replace(cached)
            finally:
                temporary.unlink(missing_ok=True)
        if digest(cached) != package["sha256"]:
            raise RuntimeError("SHA-256 mismatch in cached " + package["name"])
        with tarfile.open(cached) as archive:
            if package.get("base"):
                payload = directory / "base.txz"
                with archive.extractfile("/usr/local/share/pfSense/base.txz") as source, payload.open("wb") as output:
                    shutil.copyfileobj(source, output)
                process = subprocess.Popen(["zstd", "-dc", str(payload)], stdout=subprocess.PIPE)
                try:
                    with tarfile.open(fileobj=process.stdout, mode="r|") as base:
                        extract_payload(base, sysroot, [PurePosixPath(p) for p in
                            ["lib", "libexec", "usr/lib", "usr/include"]])
                finally:
                    process.stdout.close()
                    code = process.wait()
                if code != 0:
                    raise RuntimeError("Cannot decompress pfSense base files")
            else:
                extract_payload(archive, sysroot, [PurePosixPath(p) for p in
                    ["usr/local/lib", "usr/local/libdata", "usr/local/include"]])

    # Netgate omits sys/ from the runtime image. Use public, shared system headers
    # from FreeBSD 15's matching 1500029 revision, not a different OS release.
    headers = directory / "shared-headers"
    subprocess.run(["git", "init", "-q", str(headers)], check=True, stdout=sys.stderr)
    subprocess.run(["git", "-C", str(headers), "remote", "add", "origin", manifest["headers_repository"]], check=True)
    subprocess.run(["git", "-C", str(headers), "sparse-checkout", "set", "sys/sys"], check=True)
    subprocess.run(["git", "-C", str(headers), "fetch", "--depth=1", "--filter=blob:none", "origin", manifest["headers_revision"]], check=True)
    subprocess.run(["git", "-C", str(headers), "checkout", "-q", "--detach", "FETCH_HEAD"], check=True, stdout=sys.stderr)
    shutil.copytree(headers / "sys" / "sys", sysroot / "usr" / "include" / "sys")
    for path in ["usr/include/osreldate.h", "usr/include/sys/param.h"]:
        text = (sysroot / path).read_text()
        match = re.search(r"^#define\s+__FreeBSD_version\s+(\d+)", text, re.MULTILINE)
        if not match or int(match[1]) != manifest["osversion"]:
            raise RuntimeError("Target header version mismatch: " + path)
    for path in ["usr/lib/crt1.o", "usr/lib/libc.so", "lib/libc.so.7", "usr/local/lib/libglib-2.0.so"]:
        if not (sysroot / path).is_file():
            raise RuntimeError("Missing target development file: " + path)
    (sysroot / "pfsense-sdk.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(sysroot)


if __name__ == "__main__":
    main()
