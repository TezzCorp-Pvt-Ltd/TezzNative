# TLS client contract

`tls.connect(host, port)` verifies the certificate chain and the requested DNS
name or IP address. The secure default permits TLS 1.2 and newer. Windows uses
Schannel and the Windows trust store; Linux uses OpenSSL and its default trust
locations, including `SSL_CERT_FILE` and `SSL_CERT_DIR` when configured.

`tls.connect_with_policy(host, port, min_version, handshake_timeout_ms, pin_hex)`
uses an independent policy for one connection. It does not change or inherit the
global policy. A minimum version of `0` selects the secure default; `12` permits
TLS 1.2 and newer, and `13` requires TLS 1.3 or newer. Negative timeouts and other
minimum-version values are errors. A zero timeout selects the backend default.
The handshake timeout does not bound DNS resolution or the initial TCP connect.

An optional pin is the 64-character hexadecimal SHA-256 digest of the leaf
certificate's DER encoding. Pinning adds a check to chain and hostname
verification. An empty pin disables the additional check. Invalid policy updates
return `-1` and preserve the previous policy.

The `policy_*` functions update or inspect the runtime policy used by
`tls.connect`. A connection failure returns a null `*Tls`; `tls.last_error()`
reports a backend error. Native Windows and Linux applications import the same
runtime implementations through their deployed companion library. A required
missing library is a build error.

## Verification

Run the deterministic policy assertions with `python tests/run.py --build
--filter /net/`. Run `python tests/regression/net/check_tls.py --compiler
path/to/tezzc --backend both` for the local network checks. The latter creates
temporary self-signed certificates and uses loopback sockets only. Linux tests
trust rejection, trusted success, DNS/IP mismatches, fingerprint pins, minimum
versions, policy isolation, and stalled-handshake timeouts on both backends.
Windows runs the untrusted-certificate cases; the harness leaves the Windows
trust store unchanged.
