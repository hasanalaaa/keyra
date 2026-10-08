#!/usr/bin/env python3
"""Drive Keyra's FIDO core with python-fido2 (Yubico, BSD-2) through a fake HID link.

The host build makes `fido_harness` (firmware/components/keyra_fido/host_test):
the real CTAPHID + CTAP2/U2F code and the real vault core, speaking 64-byte HID
reports on stdin/stdout, with the button pressed automatically. This script
plugs it into python-fido2's own CTAPHID client, WebAuthn client and server,
so framing, CBOR, signatures and attestation are checked by an independent
implementation.

    python3 -m venv .venv && .venv/bin/pip install fido2
    .venv/bin/python tools/fido_harness.py <build>/keyra_fido/fido_harness
"""
import subprocess
import sys
import time
from hashlib import sha256

from fido2.attestation import AttestationType, PackedAttestation
from fido2.client import DefaultClientDataCollector, Fido2Client, UserInteraction
from fido2.client import _Ctap2ClientBackend
from fido2.ctap import CtapError
from fido2.ctap1 import Ctap1
from fido2.ctap2 import Ctap2
from fido2.hid import CtapHidDevice
from fido2.hid.base import CtapHidConnection, HidDescriptor
from fido2.server import Fido2Server
from fido2.webauthn import AttestedCredentialData

AAGUID = "b722a2aa-5acc-4835-9c91-5fa93812679d"
ORIGIN = "https://example.com"


# python-fido2's client follows CTAP 2.1 and asks for a pinUvAuthToken whenever
# an authenticator reports `uv`; Keyra is CTAP 2.0 with built-in UV while no
# PIN is set, which 2.0 platforms (browsers, Windows) drive by sending
# options.uv = true. Make the client do exactly that while clientPin is false.
_orig_auth_params = _Ctap2ClientBackend._get_auth_params


def _ctap20_auth_params(self, pin_protocol, rp_id, user_verification, permissions, allow_uv, event, on_keepalive):
    info = self.ctap2.get_info()
    if not info.options.get("clientPin") and info.options.get("uv"):
        return None, self._should_use_uv(info, user_verification, permissions)
    return _orig_auth_params(self, pin_protocol, rp_id, user_verification, permissions, allow_uv, event, on_keepalive)


_Ctap2ClientBackend._get_auth_params = _ctap20_auth_params


class PipeConnection(CtapHidConnection):
    def __init__(self, proc):
        self.proc = proc

    def write_packet(self, data):
        assert len(data) == 64, len(data)
        self.proc.stdin.write(data)
        self.proc.stdin.flush()

    def read_packet(self):
        data = self.proc.stdout.read(64)
        if len(data) != 64:
            raise IOError("harness closed")
        return data

    def close(self):
        self.proc.kill()


def open_device(path):
    proc = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    desc = HidDescriptor("harness", 0x303A, 0x8000, 64, 64, "Keyra Key", "harness")
    return CtapHidDevice(desc, PipeConnection(proc)), proc


def verify_self_attestation(attestation_object, client_data_hash):
    assert attestation_object.fmt == "packed", attestation_object.fmt
    result = PackedAttestation().verify(attestation_object.att_stmt, attestation_object.auth_data, client_data_hash)
    assert result.attestation_type == AttestationType.SELF, result.attestation_type


class Prompt(UserInteraction):
    touches = 0

    def prompt_up(self):
        Prompt.touches += 1


checks = []


def poll(fn):
    """U2F hosts repeat a request while the key answers 'conditions not satisfied'."""
    from fido2.ctap1 import APDU, ApduError
    deadline = time.monotonic() + 10
    while True:
        try:
            return fn()
        except ApduError as e:
            if e.code != APDU.USE_NOT_SATISFIED or time.monotonic() > deadline:
                raise
            time.sleep(0.1)


def check(name, cond):
    checks.append((name, bool(cond)))
    print(("PASS " if cond else "FAIL ") + name)


def main(path):
    started = time.monotonic()
    dev, proc = open_device(path)
    try:
        check("CTAPHID INIT: protocol 2, wink+cbor capabilities", dev.version == 2 and dev.capabilities & 0x05 == 0x05)
        check("PING echoes 300 bytes (multi-packet)", dev.ping(bytes(range(256)) + b"x" * 44) == bytes(range(256)) + b"x" * 44)
        dev.wink()

        ctap2 = Ctap2(dev)
        info = ctap2.info
        check("getInfo versions", info.versions == ["U2F_V2", "FIDO_2_0"])
        check("getInfo aaguid", str(info.aaguid) == AAGUID)
        check("getInfo options rk/up/uv", info.options.get("rk") and info.options.get("up") and info.options.get("uv")
              and info.options.get("plat") is False and "clientPin" not in info.options)

        # authenticatorReset is accepted only right after power-up.
        if time.monotonic() - started < 9:
            ctap2.reset()
            check("reset within 10 s of power-up", True)

        server = Fido2Server({"id": "example.com", "name": "Example"}, attestation="direct",
                             verify_attestation=verify_self_attestation)
        client = Fido2Client(dev, DefaultClientDataCollector(ORIGIN), user_interaction=Prompt())

        # 1. Second-factor credential (non-discoverable), UV discouraged.
        opts, state = server.register_begin({"id": b"user-1", "name": "hasan", "displayName": "Hasan"},
                                            user_verification="discouraged")
        reg = client.make_credential(opts["publicKey"])
        auth_data = server.register_complete(state, reg)
        cred = auth_data.credential_data
        check("register (non-resident) verified by Fido2Server, self attestation", cred is not None)
        check("credential id is 62 bytes", len(cred.credential_id) == 62)
        opts, state = server.authenticate_begin([cred])
        sel = client.get_assertion(opts["publicKey"])
        res = sel.get_response(0)
        server.authenticate_complete(state, [cred], res)
        check("authenticate with allowCredentials verified", True)

        # 2. Passkeys (discoverable), UV required, two accounts.
        creds = [cred]
        for uid, name in [(b"user-2", "alice@example.com"), (b"user-3", "bob@example.com")]:
            opts, state = server.register_begin({"id": uid, "name": name, "displayName": name.split("@")[0]},
                                                resident_key_requirement="required", user_verification="required")
            reg = client.make_credential(opts["publicKey"])
            ad = server.register_complete(state, reg)
            check(f"passkey registered for {name} (UV flag set)", ad.is_user_verified())
            creds.append(ad.credential_data)
        opts, state = server.authenticate_begin(user_verification="required")
        sel = client.get_assertion(opts["publicKey"])
        responses = [sel.get_response(i) for i in range(len(sel.get_assertions()))]
        check("discoverable sign-in returns both accounts", len(responses) == 2)
        names = sorted(a.user["id"] for a in sel.get_assertions())
        check("user handles returned", names == [b"user-2", b"user-3"])
        for r in responses:
            server.authenticate_complete(state, creds, r)
        check("discoverable assertions verified (GetNextAssertion)", True)

        # 3. Excluding an existing credential.
        opts, state = server.register_begin({"id": b"user-1", "name": "hasan"}, credentials=[cred])
        try:
            client.make_credential(opts["publicKey"])
            check("excludeCredentials refused", False)
        except Exception as e:  # ClientError(DEVICE_INELIGIBLE)
            check("excludeCredentials refused", "INELIGIBLE" in repr(e) or "EXCLUDED" in repr(e))

        # 4. Low-level CTAP2 details.
        rp_hash = sha256(b"example.com").digest()
        a = ctap2.get_assertion("example.com", b"\x01" * 32, [{"type": "public-key", "id": cred.credential_id}],
                                options={"up": False})
        check("silent assertion: UP clear, UV set", a.auth_data.flags & 0x01 == 0 and a.auth_data.flags & 0x04)
        check("rpIdHash", a.auth_data.rp_id_hash == rp_hash)
        try:
            ctap2.get_assertion("evil.example", b"\x01" * 32, [{"type": "public-key", "id": cred.credential_id}])
            check("credential bound to its RP", False)
        except CtapError as e:
            check("credential bound to its RP", e.code == CtapError.ERR.NO_CREDENTIALS)
        try:
            ctap2.send_cbor(0x01, {1: b"\x00" * 31})
            check("malformed MakeCredential refused", False)
        except CtapError as e:
            check("malformed MakeCredential refused", e.code in (CtapError.ERR.MISSING_PARAMETER,
                                                                  CtapError.ERR.INVALID_LENGTH))
        time.sleep(max(0.0, 10.5 - (time.monotonic() - started)))
        try:
            ctap2.reset()
            check("reset refused after 10 s", False)
        except CtapError as e:
            check("reset refused after 10 s", e.code == CtapError.ERR.NOT_ALLOWED)

        # 5. CTAP1 / U2F.
        ctap1 = Ctap1(dev)
        check("U2F version", ctap1.get_version() == "U2F_V2")
        app = sha256(b"https://u2f.example").digest()
        chal = sha256(b"challenge").digest()
        regd = poll(lambda: ctap1.register(chal, app))
        regd.verify(app, chal)
        check("U2F register verified (attestation cert signature)", True)
        sig = poll(lambda: ctap1.authenticate(chal, app, regd.key_handle))
        sig.verify(app, chal, regd.public_key)
        check("U2F authenticate verified, user present", sig.user_presence & 1 == 1)
        sig2 = poll(lambda: ctap1.authenticate(chal, app, regd.key_handle))
        check("signature counter increases", sig2.counter > sig.counter)
        try:
            ctap1.authenticate(chal, sha256(b"https://evil.example").digest(), regd.key_handle)
            check("U2F handle bound to its app", False)
        except Exception as e:
            check("U2F handle bound to its app", "WRONG_DATA" in repr(e) or "0x6a80" in repr(e).lower())
    finally:
        proc.kill()

    failed = [n for n, ok in checks if not ok]
    print(f"\n{len(checks) - len(failed)}/{len(checks)} checks passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
