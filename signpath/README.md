# SignPath configurations

Windows builds of `release/*` branches in `OrcaSlicer/OrcaSlicer` are code signed through [SignPath](https://about.signpath.io/), with the certificate the SignPath Foundation issues to open source projects. The XML files here are copies of the project's artifact configurations in SignPath, which the workflow refers to by slug only; a change to one has to be made in SignPath too, under the same slug.

Every signing request uses the `release-signing` policy and waits in SignPath until it is approved, so each Windows build of a release branch needs two approvals per architecture: the binaries, then the installer. Builds of `main` and pull requests are not signed.

## What gets signed

Every executable, DLL and Python extension module in the bundle ends up with a valid signature, so Windows features that check each loaded binary, such as Smart App Control, find none unsigned. Files their vendor already signed keep that signature: the Microsoft runtime DLLs and `WebView2Loader.dll` (Microsoft) and CPython's bundled OpenSSL and libffi (Python Software Foundation). Everything else is signed with the project's certificate: OrcaSlicer's own `orca-slicer.exe` and `OrcaSlicer.dll`, the libraries built from upstream sources (OCCT, FFmpeg, FreeType, GMP, MPFR, CPython), the downloaded `uv.exe`, and the NSIS installer.

The SignPath Foundation's conditions ask projects to sign only binaries built from their own source and to leave upstream projects' binaries unsigned, which they allow inside a signed installer. Signing the upstream libraries departs from that, by the maintainers' choice. Narrowing the signed set back to OrcaSlicer's own files means dropping the `third-party` file set from `windows-binaries-v1` and the third-party staging from `scripts/stage-for-signpath.ps1`.

The Foundation also requires metadata restrictions on signed files. OrcaSlicer's own binaries and the installer must have the ProductName `OrcaSlicer` and the ProductVersion passed in with the request, which the workflow takes from `SoftFever_VERSION` in `version.inc`. The binaries get that version information from `src/dev-utils/platform/msw/OrcaSlicer-version.rc.in`, the installer from `CPACK_NSIS_DEFINES` in `CMakeLists.txt`. The upstream files carry their own projects' metadata, so their file set has no restriction. `scripts/stage-for-signpath.ps1` checks the version information before a request is submitted, since SignPath only rejects a mismatch after the request has been approved.

## `windows-binaries-v1`

Signs the binaries of the installed bundle, `${{ env.BUILD_DIR }}/OrcaSlicer`. `scripts/stage-for-signpath.ps1 -Bundle` stages them for upload: `orca-slicer.exe` and `OrcaSlicer.dll` at the root of the artifact ZIP, where the metadata restrictions apply, and every other unsigned `.exe`, `.dll` and `.pyd` under `third-party/`, at its path in the bundle. `scripts/restore-from-signpath.ps1` copies the signed files back over the originals. The portable ZIP, the MSIX package and the installer are all packed from that bundle; `cpack` is pointed at it with `CPACK_INSTALLED_DIRECTORIES` rather than re-running the install rules, which would take CPython and `uv.exe` from their unsigned sources again. A check after `cpack` verifies that every binary in the installer's payload is signed.

## `windows-installer-v1`

Signs the NSIS installer `cpack` produces, `OrcaSlicer_Windows_Installer_<version>_<arch>.exe`, uploaded as the only file of its artifact. The uninstaller NSIS writes during installation is not signed.
