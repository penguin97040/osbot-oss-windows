# Third-party notices

## Bundled code

### Dear ImGui

`third_party/imgui/` contains Dear ImGui (version in `third_party/imgui/VERSION.txt`),
including the Win32 and DirectX 11 backends. `src/main.cpp` is based on its
`example_win32_directx11`.

> The MIT License (MIT)
>
> Copyright (c) 2014-2026 Omar Cornut
>
> See `third_party/imgui/LICENSE.txt` for the full licence text.

`third_party/imgui/misc/freetype/` is Dear ImGui's FreeType font loader, from the same release.

### FreeType

The app links FreeType 2.13.3 statically (downloaded at build time from the official
repository) under the FreeType Licence (FTL):

> Portions of this software are copyright © 2024 The FreeType Project (www.freetype.org).
> All rights reserved.

### Chakra Petch font

`third_party/fonts/chakra-petch/` contains Chakra Petch Regular and SemiBold, built into the exe
as resources.

> Copyright 2018 The Chakra Petch Project Authors (https://github.com/m4rc1e/Chakra-Petch.git)
>
> Licensed under the SIL Open Font Licence, Version 1.1. See
> `third_party/fonts/chakra-petch/OFL.txt` for the full licence text.

## Protocol information (no code copied)

The camera command bytes in `src/camera/ObsbotProtocol.cpp` and `docs/PROTOCOL.md`
are facts learnt from these projects. Thank you to their authors:

- [lxman/obsbot-mcp](https://github.com/lxman/obsbot-mcp) (MIT)
- [mitchelloharawild/obsbot-tiny-2-control](https://github.com/mitchelloharawild/obsbot-tiny-2-control) (MIT)
- [cgevans/tiny2](https://github.com/cgevans/tiny2) (EUPL-1.2)
- [me-tony/nod](https://github.com/me-tony/nod)
- [samliddicott/meet4k](https://github.com/samliddicott/meet4k) (LGPL-2.1)
- [vampyren/obsbot4linux](https://github.com/vampyren/obsbot4linux) (EUPL-1.2), the inspiration for this project

## Not included

- The **OBSBOT SDK** (`libdev`) is proprietary, is not used by this app, and is never
  committed to this repository.
- **Segoe UI** is only a fallback: if the built-in font can't be loaded, it's read from the user's own
  Windows installation and is not redistributed.
