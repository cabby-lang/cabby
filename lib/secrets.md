# secrets — random codes, passwords, locking and signatures

```cabby
use secrets
```

What a program needs to keep things secret:

* **random** numbers, text and passwords that no one can guess
* **passwords** kept as hashes, so a stolen file doesn't give them away
* text, bytes and files **locked** with a password
* **signatures** that show a message hasn't been changed
* **public keys**: a private key signs, and anyone can check

None of it is cryptography of Cabby's own, which is too easy to get
subtly wrong: random numbers come from the system, and the rest from a
well-tested library, found when it's first needed. On Linux that's
OpenSSL's libcrypto, which every system has (`openssl` on Arch, `libssl3`
on Debian). On a Mac it's the Mac's own CommonCrypto (or Homebrew's
OpenSSL, if CommonCrypto lacks something).

* [Random](#random)
* [Passwords](#passwords)
* [Locking](#locking)
* [Signatures](#signatures)
* [Public keys](#public-keys)
* [What's used](#whats-used)

## Random

| | |
|---|---|
| `long secrets.random_number From, To` | a whole number from From to To, both included, each as likely |
| `str secrets.random_text Length` | random letters, digits, `-` and `_`, safe in a web address or a file's name: a sign-in code, a name no one can guess (22 characters are as hard to guess as anything needs) |
| `str secrets.random_password Length` | a password for a person: small and capital letters, digits and symbols, at least one of each, without those that look alike (`0` and `O`, `1` and `l`) |
| `str secrets.random_password_without_symbols Length` | the same, without symbols |
| `bytes secrets.random_bytes Count` | random [bytes](bytes.md): a key, a salt |

These come from the system's own generator (the kernel's), which is what
anything secret needs. A game's dice can use `game`'s `random`, which is
quicker, and the same every time under a test script.

## Passwords

A program that signs people in should never keep their passwords: if the
file is ever stolen, every password goes with it, and people use the same
ones elsewhere. It keeps a **hash** of each instead, which can't be turned
back into the password, and checks a password typed against it:

<!-- run -->
```cabby
use secrets

int main {
	str Kept = secrets.hash_password "correct horse battery"   // keep this
	println Kept.starts_with "$pbkdf2-sha256$"
	println secrets.check_password("correct horse battery", Kept)
	println secrets.check_password("correct horse", Kept)
}
```

```text
true
true
false
```

| | |
|---|---|
| `str secrets.hash_password Password` | the hash, to keep: `$pbkdf2-sha256$600000$<salt>$<hash>` |
| `str secrets.hash_password_argon2 Password` | the same with Argon2id: `$argon2id$v=19$m=65536,t=3,p=4$<salt>$<hash>` (it needs OpenSSL 3.2 or later) |
| `bool secrets.can_use_argon2` | whether it can be used here |
| `bool secrets.check_password Password, Kept` | true if it's the password that was hashed, by either (or by any other program's Argon2id) |

The same password hashes differently each time (a random salt is part of
it), so two people with the same password don't have the same hash. A
hash takes a moment to make, on purpose: guessing millions of passwords
against a stolen one would take years.

Argon2id is what OWASP now prefers: each guess needs 64 MB of memory as
well as time, so even a graphics card can't guess by the million. It's in
OpenSSL 3.2 and later (Debian 13, Arch, Homebrew's), but not in a Mac's
own CommonCrypto, which is why `hash_password` is PBKDF2: a hash made
with it can be checked anywhere. Checking an Argon2id hash where there's
no Argon2 is a panic that says so.

## Locking

Text, bytes or a file, locked with a password: only the password unlocks
it, and if anything in it has been changed (a mistake in copying, or on
purpose), it won't unlock at all.

<!-- run -->
```cabby
use secrets

int main {
	str Locked = secrets.lock "The safe's code is 4812.", "hunter2"
	println Locked.length > 60       // base64 text: keep it, send it
	println secrets.unlock(Locked, "hunter2")
	println secrets.can_unlock(Locked, "hunter3")
}
```

```text
true
The safe's code is 4812.
false
```

| | |
|---|---|
| `str secrets.lock Text, Password` | the text, locked: text again (base64), to keep or send |
| `str secrets.unlock Locked, Password` | the text again; a panic if the password's wrong, or it's been changed |
| `bool secrets.can_unlock Locked, Password` | whether it would unlock |
| `bytes secrets.lock_bytes Data, Password`, `bytes secrets.unlock_bytes Locked, Password` | the same for bytes |
| `secrets.lock_file From, Into, Password` | a file, locked into another: `"diary.txt"` into `"diary.locked"` |
| `bool secrets.unlock_file From, Into, Password` | and unlocked into another; false (nothing written) if the password's wrong |

A long password (a few words) is much harder to guess than a short odd
one. There's no way back without it: a forgotten password means what's
locked is lost.

## Signatures

A web service that sends a program messages often signs each one with a
key both sides know, so the program can tell the message is the
service's own and hasn't been changed on the way:

| | |
|---|---|
| `str secrets.shared_signature Message, Key` | the message's signature: HMAC-SHA256, in hexadecimal |
| `bool secrets.check_shared_signature Message, Key, Signature` | true if it's the message's signature with that key |
| `bool secrets.same A, B` | true if the two are the same, taking as long however much of them is: for comparing codes, so the time taken gives nothing away |

## Public keys

A signature with a shared key proves nothing to a third person: anyone
with the key could have made it. With a **public key**, only the owner of
the private key can sign, and anyone given the public key that goes with
it can check. It's how a program's updates, or an SSH login, are shown to
be genuine:

<!-- check -->
```cabby
use secrets
use files

int main {
	str Private = secrets.make_private_key       // kept secret
	str Public = secrets.public_key Private      // given out
	str Update = files.read "update.txt"
	str Signature = secrets.sign Update, Private
	println secrets.check_signed(Update, Public, Signature)
	println secrets.check_signed(Update + "!", Public, Signature)
}
```

```text
true
false
```

| | |
|---|---|
| `str secrets.make_private_key` | a new private key: 32 random bytes, in base64 |
| `str secrets.public_key PrivateKey` | the public key that goes with it (base64) |
| `str secrets.sign Message, PrivateKey` | the message's signature (64 bytes, in base64) |
| `bool secrets.check_signed Message, PublicKey, Signature` | true if it was signed with the private key that goes with this public key, and is unchanged |
| `bool secrets.can_use_keys` | whether they can be used here |

They're Ed25519 keys, as SSH's newest are. They need OpenSSL (1.1.1 or
later): on a Mac, Homebrew's (`brew install openssl`), since CommonCrypto
hasn't Ed25519.

## What's used

| | |
|---|---|
| random | the system's: `getentropy`, the kernel's own generator |
| passwords | PBKDF2 with HMAC-SHA256, 600,000 rounds, a random 16-byte salt (as OWASP advises) |
| locking | AES-256 in GCM mode (encrypted, and any change noticed), its key made from the password by PBKDF2-HMAC-SHA256 (200,000 rounds, a random salt), and a random nonce: `"CBY1"`, the salt, the nonce, what's locked, then GCM's tag |
| signatures | HMAC-SHA256 |
| Argon2 passwords | Argon2id: 64 MB, 3 passes, 4 lanes, a random 16-byte salt (RFC 9106's second choice) |
| public keys | Ed25519 |

Cabby's tests check each against the standards' own examples (RFC 7914,
NIST's GCM cases, RFC 4231, RFC 8032), and Argon2 against hashes made by
its own reference program.
