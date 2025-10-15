import argparse
import base64
import os
import subprocess
import sys
from pathlib import Path
from tempfile import mkdtemp
import gnupg

# If run with tty, gpg-agent needs to know about it
try:
    tty = subprocess.check_output(["tty"], text=True, stderr=subprocess.DEVNULL).strip()
    os.environ["GPG_TTY"] = tty
    print(f"GPG_TTY set to {tty}")
except subprocess.CalledProcessError:
    print("No TTY detected; skipping GPG_TTY.")

GPG_KEY_B64 = os.environ["GPG_KEY_B64"]
GPG_KEY_PASS_B64 = os.environ["GPG_KEY_PASS_B64"]
gpg_passphrase = base64.b64decode(GPG_KEY_PASS_B64).decode("utf-8").strip()
GPG_KEY = base64.b64decode(GPG_KEY_B64 + "==").decode("utf-8").strip()
gpg_keyid = os.environ.get("GPG_KEY_ID", "252DBA8082051403AA23844DD41F3105FFB94BCF") # Techops ripple key


if not (gnupghome := os.environ.get("GNUPGHOME")):
    gnupghome = mkdtemp()

tmp_rpm_db = mkdtemp()
gnupghome.mkdir(parents=True, exist_ok=True, mode=0o0700)
gpg = gnupg.GPG(gnupghome=gnupghome)
import_result = gpg.import_keys(GPG_KEY)

print("GPG import summary:")
print(import_result.summary())
if gpg_keyid not in import_result.fingerprints:
    print(f"Failed to import secret key for {gpg_keyid}")
    sys.exit(1)

rpm_sign_cmd = [
    "rpm",
    "--define", "%__gpg /usr/bin/gpg",
    "--define", "_signature gpg",
    "--define", f"_gpg_name {gpg_keyid}",
    "--define", "__gpg_check_password_cmd /bin/true",
    "--define", "__gpg_sign_cmd %{__gpg} --batch --no-tty --no-armor --digest-algo 'sha512' --passphrase " +
    gpg_passphrase +
    " --no-secmem-warning " +
    "-u '%{_gpg_name}' " +
    "--pinentry-mode loopback " +
    "--sign --detach-sign --output %{__signature_filename} %{__plaintext_filename}",
    "--addsign",
]


def import_gpg_key_to_rpm(gpg_keyid=gpg_keyid):
    gpg_export_cmd = ["gpg", "--export", "--armor", gpg_keyid]
    key_id = subprocess.run(gpg_export_cmd, check=False, capture_output=True, text=True)
    pubkey = Path("ripple.gpg.asc")
    pubkey.write_text(key_id.stdout)
    rpm_import_cmd = ["rpm", "--dbpath", tmp_rpm_db, "--import", pubkey]
    rpm_import_result = subprocess.run(rpm_import_cmd, check=False)


def sign_package(package):
    try:
        if package.name.endswith(".rpm"):
            return sign_rpm(package)
        if package.name.endswith(".deb"):
            return sign_deb(package)
        else:
            print(f"couldn't determine file type of {package}")
    except Exception as e:
        print(f"Something went wrong! {e}")
        sys.exit(1)


def sign_rpm(package):
    rpm_sign_cmd.append(package)
    result = subprocess.run(rpm_sign_cmd, check=False, capture_output=True, text=True, input="y")
    if result.returncode != 0:
        print(result.stderr)
        sys.exit(1)
    return result


def verify_package_signature(pkg):
    if pkg.name.endswith("rpm"):
        import_gpg_key_to_rpm()
        verify = verify_rpm_package_signature
    elif pkg.name.endswith("deb"):
        verify = verify_deb_package_signature
    return verify(pkg)


def verify_deb_package_signature(package):
    signature = Path(f"{package.name}.asc")
    try:
        with signature.open(mode="rb") as fp:
            verified = gpg.verify_file(fp, package)
        print(verified.stderr)
        if verified.status == 0 and verified.fingerprint == gpg_keyid:
            print(f"✅ {verified.status} for {verified.username} - {verified.pubkey_fingerprint}")
    except Exception:
        print(f"❌ Signature verification failed for {package}")
        sys.exit(1)

def verify_rpm_package_signature(pkg):
    cmd = ["rpm","--dbpath", tmp_rpm_db, "-Kv", pkg]
    result = subprocess.run(cmd, check=False, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(1)
    else:
        print(result.stdout)
        return True


def sign_deb(package):
    package_name = package.name
    signature = Path(f"{package_name}.asc")
    print(f"package_name: {package_name}")
    print(f"signature_file: {signature}")
    cmd = [
        "gpg",
        "--batch",
        "--yes",
        "--armor",
        "--passphrase", gpg_passphrase,
        "--pinentry-mode", "loopback",
        "--output", signature,
        "--detach-sign",
        package,
    ]

    result = subprocess.run(cmd, check=False)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("package")
    args = parser.parse_args()
    package = Path(args.package)
    sign_package(package)
    verify_package_signature(package)
