#!/usr/bin/env python3
"""Known-answer test for tycho-rsa's RSAES-OAEP, against python's `cryptography`.

Both directions, because either alone can pass on a self-consistent wrong
implementation: an encoder and a decoder that share a mistake round-trip
perfectly. Only an EXTERNAL peer settles which of the two is RFC 8017.

  usage: kat.py <path to tycho-rsa binary>
Prints one line per leg and exits non-zero on any mismatch.
"""
import subprocess
import sys

try:
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import padding, rsa
except ImportError:
    print("kat: SKIP -- python 'cryptography' not installed")
    sys.exit(0)

RSA = sys.argv[1]

N = 124602906642897286956645496574165116941171778557597699444509060620139064709177676100898148580450805396236455991351844910686750050501537803986435163672222921990038549831676875856029279872640104858672986107746643839826465506588729084785544814772352797429279210100898951761667676083519209212528469004518949353199
D = 101985512233267518648113601198327525486998113639746985609700024729921717046324972515194128324518388883524112117309372001375527708911805382358612213170296299118492331093023912757439140028970287451512649927141348257912746522698629729415559296882581421576915423845199249970863869644784601246283573842210648331521
E = 65537
P = 12295167561878370053136757847621661287273501488146994070824701424726117061427684228375940908479680592334685383298450543681609522885730565241623257210985857
Q = 10134299188343987306483849107535208684129236960232614347762448400358814724338059771545190502899320874080491058181501491336154609130628032715666398867434607

OAEP = padding.OAEP(mgf=padding.MGF1(algorithm=hashes.SHA256()),
                    algorithm=hashes.SHA256(), label=None)

pub = rsa.RSAPublicNumbers(E, N)
priv = rsa.RSAPrivateNumbers(
    p=P, q=Q, d=D, dmp1=D % (P - 1), dmq1=D % (Q - 1),
    iqmp=pow(Q, -1, P), public_numbers=pub).private_key()

MSGS = [b"", b"Hello, world!", bytes(range(62))]
fail = 0

for m in MSGS:
    mh = m.hex()
    # ---- direction A: python encrypts, tycho decrypts -----------------------
    ct = priv.public_key().encrypt(m, OAEP)
    got = subprocess.run([RSA, "decrypt", ct.hex(), str(D), str(N)],
                         capture_output=True, text=True)
    got_hex = got.stdout.strip()
    ok = got.returncode == 0 and got_hex == mh
    print("kat A python->tycho (%2d bytes): %s  got=%s" %
          (len(m), "ok" if ok else "FAIL", got_hex or got.stderr.strip()))
    fail |= 0 if ok else 1

    # ---- direction B: tycho encrypts, python decrypts ------------------------
    enc = subprocess.run([RSA, "encrypt", mh, str(E), str(N)],
                         capture_output=True, text=True)
    if enc.returncode != 0:
        print("kat B tycho->python (%2d bytes): FAIL  %s" % (len(m), enc.stderr.strip()))
        fail = 1
        continue
    try:
        back = priv.decrypt(bytes.fromhex(enc.stdout.strip()), OAEP)
    except Exception as exc:
        print("kat B tycho->python (%2d bytes): FAIL  %s" % (len(m), exc))
        fail = 1
        continue
    ok = back == m
    print("kat B tycho->python (%2d bytes): %s  got=%s" % (len(m), "ok" if ok else "FAIL", back.hex()))
    fail |= 0 if ok else 1

sys.exit(1 if fail else 0)
