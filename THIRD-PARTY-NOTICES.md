# External dependency attribution

The plugin's project license remains undecided. The following licenses apply to their respective external dependencies; they do not select a license for this project. Dependency source trees and compiled libraries are not included in this source distribution.

| Dependency | License / attribution | Upstream notice |
|---|---|---|
| Pinned CommonLibSSE-NG | MIT; Ryan-rsm-McKenzie and upstream contributors | [CommonLib license](https://github.com/CharmedBaryon/CommonLibSSE-NG/blob/c4672d5d70183f151e1dc470e73a747b78511304/LICENSE) |
| Pinned OpenVR headers | Valve BSD-style redistribution license; Valve Corporation | [OpenVR license](https://github.com/ValveSoftware/openvr/blob/ebdea152f8aac77e9a6db29682b81d762159df7e/LICENSE) |
| spdlog | MIT; Gabi Melman and spdlog contributors | [spdlog license](https://github.com/gabime/spdlog/blob/v1.x/LICENSE) |
| Bundled fmt | MIT; Victor Zverovich | [fmt license](https://github.com/fmtlib/fmt/blob/master/LICENSE) |
| rapidcsv | BSD-3-Clause; Kristofer Berggren | [rapidcsv license](https://github.com/d99kris/rapidcsv/blob/master/LICENSE) |
| DirectXTK / DirectXMath | MIT; Microsoft Corporation | [DirectXTK](https://github.com/microsoft/DirectXTK/blob/main/LICENSE), [DirectXMath](https://github.com/microsoft/DirectXMath/blob/main/LICENSE) |

Keep the actual copyright/license/disclaimer notices from the versions used when redistributing dependency source, incorporated code or compiled binaries. The BSD-style terms also require retaining/reproducing the applicable notices and restrict endorsement. For a future binary package, preserve applicable full notices alongside the package, including notices for statically incorporated dependencies. Review any separately redistributed Microsoft runtime components under their own redistribution terms. No Microsoft runtime DLL is bundled here.

The license evidence was checked against the locally installed/pinned dependency copies. Moving upstream links are explanatory; they do not authorize changing the verified dependency revisions. Xbyak support and CommonLib tests were OFF in the verified plugin build, so their optional dependencies are not part of that tested configuration.
