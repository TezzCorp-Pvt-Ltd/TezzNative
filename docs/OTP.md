# One-time passwords

The SDK `otp` module implements HMAC-SHA1 HOTP (RFC 4226) and 30-second
TOTP (RFC 6238), using Windows CNG or OpenSSL. `generate_secret()` returns
an owned 32-character Base32 encoding of 160 random bits. Numeric verification
codes use the platform CSPRNG and rejection sampling, with lengths bounded to
128 digits. There is no clock-seeded generator, default secret, or substitute
algorithm when cryptography is unavailable.

`generate_hotp(secret, counter, digits)` and
`generate_totp_at(secret, unix_seconds, digits)` accept 6 through 8 digits.
Secrets use RFC 4648 Base32, with optional canonical padding and a minimum
decoded length of 128 bits. Whitespace, invalid characters, nonzero trailing
bits, invalid padding, and shorter keys are rejected. Returned strings are
owned by the caller and must be freed; null indicates invalid input or failure.
The former 80-bit, 16-character generated secrets must be re-enrolled.

`generate_totp` and `verify_totp` retain the six-digit, 30-second API.
`verify_totp_at` permits deterministic verification at a supplied Unix time.
The drift window is bounded to ten steps on either side; the compatibility
verification API treats a negative window as one step.

Verification alone is stateless. Applications must enroll secrets for an
authenticated identity, protect secrets at rest, limit attempts, and atomically
reject/store reused moving factors. `matched_step(secret, code, unix_seconds,
window, digits)` returns the matching step, including zero, or -1 on failure.
Store the last accepted step for each identity and accept a result only when
it is greater than that stored value. Concurrent verification requires an
atomic comparison/update in the application's replay store.

`provisioning_uri` validates the key and percent-encodes account and issuer
components. Its returned URI contains the secret and must be treated as a
credential. The development HTTP server does not provide enrollment or
verification endpoints without an application identity and replay policy.

References: [RFC 4226](https://www.rfc-editor.org/rfc/rfc4226),
[RFC 6238](https://www.rfc-editor.org/rfc/rfc6238),
[RFC 4648](https://www.rfc-editor.org/rfc/rfc4648).
