# Third-party code in this folder

The Mandarin save cipher in this folder is ported from **MandarinJuice** by Michał Gębicki (mi5hmash).

- Source: https://github.com/mi5hmash/MandarinJuice
- Version: `main` at commit `11a7e99c2189a99b6ee0a874d2c4ffc6221a5942` (fetched 2026-11-05); the game profiles come from the v1.4.1 release (`_profiles.zip`).
- Licence: MIT, copyright (c) 2026 Michał Gębicki. The text is below and is kept at the top of each ported file as a notice.

What was taken: the cipher of `Helpers/MandarinDeencryptor.cs` (slices, header key wrap, AES stream, slice checksum, MurmurHash3 signature), the file layout of `Models/DSSS/Mandarin/*` (header, footer, flavors, compression) and the user id variants of `GamingPlatformsFactory/Platforms/Steam.cs`. The 2048-bit container arithmetic is replaced by `System.Numerics.BigInteger`, the AES stream by `System.Security.Cryptography.Aes`; output was compared byte for byte with MandarinJuice's own code. Nothing of its UI, command line or profile files is used: the per-game parameters (seed, variant) are in the game profile (`launcher/games/*.json`, key `saveSync.resign`).

## Licence text

MIT License

Copyright (c) 2026 Michał Gębicki

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
