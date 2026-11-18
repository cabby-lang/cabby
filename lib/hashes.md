# hashes — fingerprints of text and files

```cabby
use hashes
```

A hash is a fingerprint: the same text (or file) always gives the same
one, and any change, even one letter, gives a quite different one. So a
hash says whether two things are the same without comparing them byte by
byte, and whether a download arrived whole: websites that offer big
files (a Linux ISO, say) show each file's SHA-256 to compare with.

<!-- run -->
```cabby
use hashes

int main {
	println hashes.sha256 "Salve!"
	println hashes.sha256 "Salve?"
	println hashes.md5 "Salve!"
}
```

```text
61ee6cad977244872566817a3499af9bd18548798437dd11ab239863bcaa705f
f5f55d612bea42dac1bc5bee1b903eff58f729564e39836eae07cf8187c57969
ff940ba204d233c254bab9de1e0f0618
```

| Function | What it gives |
|---|---|
| `str hashes.sha256 const str &Text` | SHA-256 of the text: 64 hexadecimal digits, in small letters |
| `str hashes.sha256_of_file const str &Path` | SHA-256 of a file's bytes |
| `str hashes.sha1 const str &Text`, `str hashes.sha1_of_file const str &Path` | SHA-1: 40 digits |
| `str hashes.md5 const str &Text`, `str hashes.md5_of_file const str &Path` | MD5: 32 digits |

The text can hold any bytes, not only letters. A file can be any size:
it's read a piece at a time, so it never has to fit in memory. A file
that isn't there, or can't be read, is a panic at the line that asked.

**Which one.** SHA-256 is the one to use. SHA-1 and MD5 are older, and
someone who wanted to could make two different files with the same
SHA-1 or MD5: they're here for checking against websites that only show
those. None of them is for keeping passwords (that needs a slow hash,
made for it).

Tutorial 48 and chapter 48 of the manual use them: checking a download,
and finding which files in a folder have changed.

## How it works

The three are written into Cabby's runtime (`runtime/hashes.c`), from
their published standards (FIPS 180-4 for the SHAs, RFC 1321 for MD5), and
checked against those standards' own examples, so nothing needs
installing.

## Passwords and random numbers

For those, see [secrets](secrets.md): random numbers and codes no one can
guess, and passwords hashed slowly, as they should be (PBKDF2 or
Argon2id), never with these.
