#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.13"
# dependencies = [
#     "python-gnupg",
# ]
# ///
import argparse
import base64
import gnupg
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


@dataclass(slots=True)
class SignCfg:
    gnupghome: Path
    fingerprint: str
    passphrase: str


def set_tty():
    try:
        tty = subprocess.check_output(["tty"], text=True, stderr=subprocess.DEVNULL).strip()
        os.environ["GPG_TTY"] = tty
        # print(f"GPG_TTY set to {tty}")
    except subprocess.CalledProcessError:
        print("No TTY detected. Skipping setting GPG_TTY.")


def make_cfg(passphrase: str, armored_private_key: str) -> SignCfg:
    ghome = Path(tempfile.mkdtemp())
    ghome.chmod(0o700)
    gpg = gnupg.GPG(gnupghome=str(ghome))
    imp = gpg.import_keys(armored_private_key)
    fp = imp.fingerprints[0]
    return SignCfg(gnupghome=ghome, fingerprint=fp, passphrase=passphrase)


def import_pubkey_into_rpmdb(gnupghome: Path, fingerprint: str, rpmdb: Path):
    env = {**os.environ, "GNUPGHOME": str(gnupghome)}
    cp = subprocess.run(
        ["gpg", "--batch", "--yes", "--armor", "--export", fingerprint],
        env=env, text=True, capture_output=True, check=True,
    )
    pub = rpmdb / "pubkey.asc"
    pub.write_text(cp.stdout)

    rpmdb.mkdir(parents=True, exist_ok=True)
    subprocess.run(["rpm", "--dbpath", str(rpmdb), "--import", str(pub)], check=True)


def sign_rpm(pkg: Path, cfg: SignCfg) -> subprocess.CompletedProcess:
    fd, pfile = tempfile.mkstemp(text=True)
    os.write(fd, cfg.passphrase.rstrip("\r\n").encode()); os.close(fd); os.chmod(pfile, 0o600)
    rpm_sign_cmd = [
        "rpm",
        "--define", "%__gpg /usr/bin/gpg",
        "--define", "_signature gpg",
        "--define", f"_gpg_name {cfg.fingerprint}",
        "--define", f"_gpg_path {cfg.gnupghome}",
        "--define", f"_gpg_passfile {pfile}",
        "--define", "__gpg_check_password_cmd /bin/true",
        "--define",
            "__gpg_sign_cmd %{__gpg} --batch --no-tty --no-armor "
            "--digest-algo sha512 --pinentry-mode loopback "
            "--passphrase-file %{_gpg_passfile} "
            "-u '%{_gpg_name}' --sign --detach-sign "
            "--output %{__signature_filename} %{__plaintext_filename}",
        "--addsign", str(pkg),
    ]

    return subprocess.run(
        rpm_sign_cmd,
        text=True,
        check=False,
        capture_output=True,
    )


def sign_deb(pkg: Path, cfg: SignCfg) -> subprocess.CompletedProcess:
    sig = pkg.with_suffix(pkg.suffix + ".asc")
    env = {**os.environ, "GNUPGHOME": str(cfg.gnupghome)}
    return subprocess.run(
        [
            "gpg",
            "--batch", "--yes", "--armor",
            "--pinentry-mode", "loopback",
            "--local-user", cfg.fingerprint,
            "--passphrase", cfg.passphrase,
            "--output", str(sig),
            "--detach-sign", str(pkg),
        ],
        env=env, check=False, capture_output=True, text=True,
    )


def sign_package(pkg: Path, cfg: SignCfg) -> subprocess.CompletedProcess:
    if pkg.suffix == ".rpm":
        return sign_rpm(pkg, cfg)
    if pkg.suffix == ".deb":
        return sign_deb(pkg, cfg)
    raise ValueError(f"unsupported package type: {pkg}")


def verify_signature(pkg: Path, *, gnupghome: Path, expected_fp: str):
    print(f"Verifying {pkg.resolve()}")
    suf = pkg.suffix.lower()
    if suf == ".rpm":
        return verify_rpm_signature(pkg, gnupghome=gnupghome, expected_fp=expected_fp)
    elif suf == ".deb":
        return verify_deb_signature(pkg, gnupghome=gnupghome, expected_fp=expected_fp)
    else:
        raise ValueError(f"unsupported package type: {pkg}")


def verify_deb_signature(pkg: Path, gnupghome: Path, expected_fp: str) -> None:
    pkg = Path(pkg)
    sig = pkg.with_suffix(pkg.suffix + ".asc")
    env = {**os.environ, "GNUPGHOME": str(gnupghome)}
    VALIDSIG_RE = re.compile(r"\[GNUPG:\]\s+VALIDSIG\s+([0-9A-Fa-f]{40})")
    verify_cmd = ["gpg", "--batch", "--status-fd", "1", "--verify", str(sig), str(pkg)]
    result = subprocess.run(verify_cmd, env=env, text=True, capture_output=True)

    if result.returncode != 0:
        print(result.stderr or result.stdout)
        sys.exit(result.returncode)

    m = VALIDSIG_RE.search(result.stdout)
    if not m or m.group(1).upper() != expected_fp.upper():
        print(f"Signature invalid or wrong signer. Expected {expected_fp}")
        sys.exit(result.returncode)
    print("********* deb signature verification *********")
    print(f"✅ Signature verified for {pkg.name} ({m.group(1)})")


def verify_rpm_signature(pkg: Path, *, gnupghome: Path, expected_fp: str):
    env = {**os.environ, "GNUPGHOME": str(gnupghome)}
    export_cmd = ["gpg", "--batch", "--yes", "--armor", "--export", expected_fp]
    cp = subprocess.run(export_cmd, env=env, text=True, capture_output=True, check=True)
    rpmdb = Path(tempfile.mkdtemp())
    try:
        pub = rpmdb / "pubkey.asc"
        pub.write_text(cp.stdout)
        # rpm needs the rpmdb for verification
        subprocess.run(["rpm", "--dbpath", str(rpmdb), "--import", str(pub)], check=True)
        verify_cmd = ["rpm", "--dbpath", str(rpmdb), "-Kv", str(pkg)]
        result = subprocess.run(verify_cmd, text=True, capture_output=True)
        if result.returncode != 0:
            print(result.stdout or result.stderr)
            sys.exit(result.returncode)
        print("********* rpm signature verification *********")
        print(result.stdout)
        print(f"✅ Signature verified for {pkg.name}")
        return True
    finally:
        try:
            for p in rpmdb.iterdir(): p.unlink()
            rpmdb.rmdir()
        except Exception:
            pass


def main():
    set_tty()
    GPG_KEY_B64 = os.environ["GPG_KEY_B64"]
    GPG_KEY_PASS_B64 = os.environ["GPG_KEY_PASS_B64"]
    gpg_passphrase = base64.b64decode(GPG_KEY_PASS_B64).decode("utf-8").strip()
    gpg_key = base64.b64decode(GPG_KEY_B64).decode("utf-8").strip()

    parser = argparse.ArgumentParser()
    parser.add_argument("package")
    args = parser.parse_args()
    cfg = make_cfg(passphrase=gpg_passphrase, armored_private_key=gpg_key)
    try:
        pkg = Path(args.package)
        res = sign_package(pkg, cfg)
        if res.returncode:
            print(res.stderr.strip() or res.stdout.strip())
            raise sys.exit(res.returncode)
        verify_signature(pkg, gnupghome=cfg.gnupghome, expected_fp=cfg.fingerprint)
    finally:
        shutil.rmtree(cfg.gnupghome, ignore_errors=True)
    sys.exit(0)


if __name__ == "__main__":
    main()
