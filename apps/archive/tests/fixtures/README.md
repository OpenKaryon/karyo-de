# Encrypted RAR fixtures

`data.rar` and `header.rar` are the corresponding
`test_read_format_rar_encryption_{data,header}.rar.uu` fixtures decoded from
[libarchive v3.7.4](https://github.com/libarchive/libarchive/tree/v3.7.4/libarchive/test).
The public test password is `12345678`. Both contain encrypted `foo.txt` and
`bar.txt` (16 bytes each); the latter also encrypts headers.

Original license, reproduced from the accompanying test sources:

```text
/*-
 * Copyright (c) 2013 Konrad Kleine
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
```

`implicit-folders.zip` is an original public QA fixture generated with Python's
standard-library zipfile. It contains three four-byte files, no directory
records, UTF-8 names and Unix permission attributes without explicit file-type
bits, matching normal ZipFile.writestr output. Its date is fixed; no personal
content is included. Karyo contributors license this fixture GPL-3.0-or-later.
