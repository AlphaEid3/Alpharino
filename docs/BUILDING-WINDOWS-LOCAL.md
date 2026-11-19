# Building Alpharino on Windows

These are the tools the **Build Windows x64** job uses on `windows-latest`
(image `windows-2025-vs2026`, version `20261116.315.1`, recorded from
[run 44459084377](https://github.com/AlphaEid3/Alpharino/actions/runs/44459084377)).
Install the same versions so a local build matches CI.

The job does not use vcpkg for Alpharino's libraries. Dependencies come from
Conan. OpenSSL is the Conan package, not a separate installer.

## Tools

| Tool          | Version in CI                                        | Where it lives                                                                                                                  |
| ------------- | ---------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------- |
| Windows       | Windows Server 2025, 10.0.26100                      | —                                                                                                                               |
| Visual Studio | Enterprise 2026, 18.12 (`VSCMD_VER` 18.12.0)         | `C:\Program Files\Microsoft Visual Studio\18\Enterprise`                                                                        |
| MSVC          | 19.52.37029.0, toolset 14.52.37025                   | `...\VC\Tools\MSVC\14.52.37025`                                                                                                 |
| Windows SDK   | 10.0.26100.0                                         | Windows Kits 10                                                                                                                 |
| CMake         | 4.5.0                                                | `C:\Program Files\CMake\bin`                                                                                                    |
| Ninja         | 1.13.2                                               | On `PATH`. Visual Studio also ships a copy under `Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja`                           |
| Python        | 3.13 (the job's `actions/setup-python` version)      | Used only to `pip install` Conan                                                                                                |
| Conan         | 2.28.1                                               | `pip install conan==2.28.1`                                                                                                     |
| Qt            | 6.9.3, architecture `win64_msvc2022_64`              | CI: `D:\a\Alpharino\Qt\6.9.3\msvc2022_64`. Local installer path: `C:\Qt\6.9.3\msvc2022_64`                                      |
| aqtinstall    | 3.3.0 (what `jurplel/install-qt-action@v4.3.1` runs) | Only needed if you install Qt from the command line                                                                             |
| OpenSSL       | Conan `openssl/3.6.1`, shared (`with_openssl3=True`) | Built or restored by `conan install`. The Windows package copies `libssl-3-x64.dll` and `libcrypto-3-x64.dll` from this package |
| sccache       | 0.14.0                                               | https://github.com/mozilla/sccache/releases/download/v0.14.0/sccache-v0.14.0-x86_64-pc-windows-msvc.tar.gz                      |
| Git           | Current Git for Windows, submodules required         | `git clone --recurse-submodules`                                                                                                |

### Visual Studio workload

Install **Visual Studio 2026 Build Tools** (or Enterprise) and select:

- Workload **Desktop development with C++** (`Microsoft.VisualStudio.Workload.NativeDesktop`)
- MSVC build tools, the 14.52 toolset (MSVC 19.52)
- Windows 11 SDK 10.0.26100
- C++ CMake tools for Windows (brings Ninja if you do not install it separately)

Open **x64 Native Tools Command Prompt for VS 2026** or **Developer PowerShell for VS 2026** before the commands below. That prompt is what `Chatterino/msvc-dev-cmd@v2.0.5` with `arch: x64` sets up in CI.

### Qt 6.9.3

In the Qt installer, under Qt 6.9.3, select **MSVC 2022 64-bit**. That component installs to:

`C:\Qt\6.9.3\msvc2022_64`

Also select **Qt Image Formats** (`qtimageformats`). Do not add `qtsvg` as an extra module: it is already inside the base Qt archive, and aqtinstall 3.3 rejects it as a module ("not found while parsing XML"). CI passes `modules: qtimageformats` to `jurplel/install-qt-action@v4.3.1` for that reason.

Add `C:\Qt\6.9.3\msvc2022_64\bin` to `PATH` so `windeployqt` is found.

The same install with aqtinstall 3.3.0:

```bat
pip install "aqtinstall==3.3.0"
aqt install-qt windows desktop 6.9.3 win64_msvc2022_64 -m qtimageformats -O C:\Qt
```

## One-time setup

```bat
python -m pip install "conan==2.28.1"
conan --version
conan profile detect -f
```

Put `sccache.exe` 0.14.0 on `PATH`. In the same Developer prompt, pick a cache directory and size once per session:

```bat
set SCCACHE_DIR=%LOCALAPPDATA%\alpharino-sccache
set SCCACHE_CACHE_SIZE=4G
sccache --start-server
```

`SCCACHE_CACHE_SIZE` matches the CI limit (`max-size: 4G` on `hendrikmuhs/ccache-action@v1.2.23`).

## Configure and build

From the repository root, in the Developer prompt. The first configure downloads and builds Conan packages (Boost 1.90.0, OpenSSL 3.6.1, FFmpeg 7.1.5, and the rest of `conanfile.py`). Later configures reuse `%USERPROFILE%\.conan2`.

```bat
git submodule update --init --recursive
mkdir build
cd build
conan install .. -s build_type=RelWithDebInfo -c tools.cmake.cmaketoolchain:generator="Ninja" -b missing -c tools.build:jobs=4 --output-folder=. -o with_openssl3=True
cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER_LAUNCHER=sccache -DCMAKE_CXX_COMPILER_LAUNCHER=sccache -DCMAKE_PREFIX_PATH=C:\Qt\6.9.3\msvc2022_64 -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake -DUSE_PRECOMPILED_HEADERS=OFF -DBUILD_WITH_CRASHPAD=Off -DCHATTERINO_LTO=OFF -DFORCE_JSON_GENERATION=Off -DSKIP_JSON_GENERATION=Off -DCHATTERINO_NIGHTLY_BUILD=On -DCHATTERINO_SPELLCHECK=On -DMOLTORINO_VELOPACK=Off ..
cmake --build . --parallel 4
sccache --show-stats
```

CMake also detects `sccache` or `ccache` on `PATH` and uses it as the C++ launcher. When it does, it rewrites MSVC `/Zi` to `/Z7` for both C and C++, because a shared PDB cannot be cached and sccache drops `/FS`. Passing `-DCMAKE_C_COMPILER_LAUNCHER` and `-DCMAKE_CXX_COMPILER_LAUNCHER` is what CI does as well.

`USE_PRECOMPILED_HEADERS` is OFF in this command. sccache 0.14 refuses MSVC precompiled-header compilations (`/Fp`, `/Yc`), so a clean build would recompile everything and the cache would stay empty. CI sets the same option. The `/pathmap` remapping flags are also left off when `sccache` or `ccache` is the launcher: sccache 0.14 reads each unknown `/pathmap` option as a second input file and then refuses the compile.

The executable is `build\bin\Alpharino.exe`.

## Incremental build

Leave the `build` directory in place. After editing sources, run:

```bat
cmake --build build --parallel 4
```

Ninja rebuilds only the targets whose inputs changed. For that local loop you can turn precompiled headers back on (`-DUSE_PRECOMPILED_HEADERS=ON`) and skip sccache; Ninja will not rebuild unchanged files.

sccache matters when the `build` directory is new, which is every GitHub-hosted run. Unchanged translation units are served from `%SCCACHE_DIR%` instead of invoking `cl.exe`. `sccache --show-stats` prints `Cache hits` and `Cache misses`. A hit rate near zero with `Non-cacheable reasons: /Fp` means precompiled headers are still on.

CI keeps three caches between runs on the same branch:

- sccache, saved on every Build Windows x64 run (`hendrikmuhs/ccache-action`, variant `sccache`, 4G, timestamped key with a stable restore prefix)
- Qt 6.9.3 (`install-qt-action` `cache: true`, prefix `Windows-QtCache-6.9.3-v3`)
- Conan packages (`actions/cache` on `~/.conan2`, key includes the `conanfile.py` hash, with a restore prefix)

CI already configures with `-G Ninja`.
