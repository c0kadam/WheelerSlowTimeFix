# External dependency attribution

The plugin project is licensed under GPL-3.0-or-later; see [LICENSE](LICENSE). The following licenses continue to apply to their respective external dependencies and are not replaced by the project license. Dependency source trees and compiled libraries are not included in this source distribution.

| Dependency | License / attribution | Upstream notice |
|---|---|---|
| CommonLibSSE-NG v13.1.0, `12d61f3c39f7d4ba1b1ab543df2bca0e613dc747` | GPL-3.0-or-later, with the Modding Exception and GPL-3.0 Linking Exception (with Corresponding Source); upstream authors and contributors | [COPYING.txt](https://github.com/alandtse/CommonLibSSE-NG/blob/12d61f3c39f7d4ba1b1ab543df2bca0e613dc747/COPYING.txt), [EXCEPTIONS.md](https://github.com/alandtse/CommonLibSSE-NG/blob/12d61f3c39f7d4ba1b1ab543df2bca0e613dc747/EXCEPTIONS.md) |
| CommonLibSSE-NG inherited MIT code | MIT; Copyright (c) 2018 Ryan-rsm-McKenzie | [licenses/LICENSE-MIT.txt](https://github.com/alandtse/CommonLibSSE-NG/blob/12d61f3c39f7d4ba1b1ab543df2bca0e613dc747/licenses/LICENSE-MIT.txt) |
| OpenVR headers, `60eb187801956ad277f1cae6680e3a410ee0873b` | Valve BSD-style redistribution license; Copyright (c) 2015, Valve Corporation | [OpenVR license](https://github.com/ValveSoftware/openvr/blob/60eb187801956ad277f1cae6680e3a410ee0873b/LICENSE) |
| MinHook v1.3.4 / hde64, `c3fcafdc10146beb5919319d0683e44e3c30d537` | BSD-style two-clause terms; MinHook Copyright (C) 2009-2017 Tsuda Kageyu; Hacker Disassembler Engine 64 Copyright (c) 2008-2009, Vyacheslav Patkov | [MinHook and HDE notices](https://github.com/TsudaKageyu/minhook/blob/c3fcafdc10146beb5919319d0683e44e3c30d537/LICENSE.txt) |
| spdlog | MIT; Gabi Melman and spdlog contributors | [spdlog license](https://github.com/gabime/spdlog/blob/v1.x/LICENSE) |
| Bundled fmt | MIT; Victor Zverovich | [fmt license](https://github.com/fmtlib/fmt/blob/master/LICENSE) |
| rapidcsv | BSD-3-Clause; Kristofer Berggren | [rapidcsv license](https://github.com/d99kris/rapidcsv/blob/master/LICENSE) |
| DirectXTK / DirectXMath | MIT; Microsoft Corporation | [DirectXTK](https://github.com/microsoft/DirectXTK/blob/main/LICENSE), [DirectXMath](https://github.com/microsoft/DirectXMath/blob/main/LICENSE) |

Keep the actual copyright, license terms and disclaimers from the versions used when redistributing dependency source, incorporated code or compiled binaries. Preserve the inherited CommonLib MIT notice as well as the current GPL terms and exceptions. Include the applicable full MinHook and HDE64 notices, not just a license label. OpenVR's BSD-style terms also restrict endorsement. Review any separately redistributed Microsoft runtime components under their own redistribution terms. No Microsoft runtime DLL is bundled here.

The pinned CommonLib patch safety implementation compiles hde64 from MinHook. Xbyak support and CommonLib tests are disabled in the release configuration. Links to moving upstream branches are explanatory; use the actual versions and notices incorporated into the distributed DLL.

## Compiled DLL distribution and corresponding source

GitHub distributes the project's release source archives; compiled DLL packages are distributed separately through Nexus Mods. This separation does not remove the license and source requirements for the compiled package.

[GPLv3 section 6(d)](https://www.gnu.org/licenses/gpl-3.0.html#section6) permits corresponding source on a different server under its stated conditions, including equivalent copying access, clear directions beside the binary download and continued source availability. A Nexus DLL download must identify the matching project source revision and where to obtain the required corresponding source at no additional charge.

CommonLib's linking exception expressly requires corresponding source for the covered work and the parts of Modding Libraries used. Before distributing a DLL, identify its exact incorporated components and provide the source and build material required by GPLv3 and those exceptions, including the relevant dependency versions and any modifications. Retain all applicable notices with the compiled package. The original upstream texts govern; this summary does not replace them.

The project-only GitHub archive is not asserted to contain the complete corresponding source for the statically linked DLL. The complete source set, any applicable exclusions and the actual download/delivery arrangement must be verified for the final Nexus package before publication. These links and notices alone do not establish binary-distribution compliance.
