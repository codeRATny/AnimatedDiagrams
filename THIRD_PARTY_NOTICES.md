# Third-party notices

## resources/pptx/template.pptx

The default presentation template of [python-pptx](https://github.com/scanny/python-pptx)
(`pptx/templates/default.pptx`), used as the base of new PowerPoint presentations.

```
The MIT License (MIT)
Copyright (c) 2013 Steve Canny, https://github.com/scanny

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

## third_party/merman (Mermaid import)

[merman](https://github.com/Latias94/merman) 0.8.0, a Rust implementation of Mermaid (MIT OR Apache-2.0),
built from crates.io and linked statically through its C ABI (`merman-ffi`; the headers in
`third_party/merman/include` come from that release). Used under the MIT license
(`third_party/merman/LICENSE-MIT`). Its Rust dependencies (MIT, Apache-2.0, Unicode-3.0, BSD-3-Clause, Zlib,
BSL-1.0 and, unmodified, MPL-2.0) are listed with their licenses in `CRATES.md` (installed to `merman/` next to this file).
