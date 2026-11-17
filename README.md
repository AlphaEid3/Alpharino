<img src="resources/icon.png" alt="Alpharino" width="128">

# Alpharino

Alpharino is AlphaEid3's Windows Twitch chat client. It is a fork of [Moltorino](https://github.com/EEVA-inc/Moltorino), which is a fork of [Chatterino7](https://github.com/SevenTV/chatterino7), which is based on [Chatterino 2](https://github.com/Chatterino/chatterino2). Existing Moltorino features stay in place, including YouTube, Kick, and TikTok chat, polls and predictions, the theme editor, vanity, GIF search, FFZ and BTTV emote modifiers, translation, and local chat recording.

Windows is the supported target. Linux, macOS, and FreeBSD build notes are still in the repository for reference.

### Downloads

Windows portable builds are published on the [GitHub releases](https://github.com/AlphaEid3/Alpharino/releases) page. Pull requests also upload `alpharino-windows-x64-portable.zip` as a GitHub Actions artifact.

Alpharino does not check Moltorino's update service. Choosing **check for updates** opens this repository's releases page.

You may need the [VC++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe) if it is not already installed.

### Settings

Alpharino stores settings, cache, and logs in its own folder:

`%APPDATA%\Alpharino`

That path is separate from `%APPDATA%\Chatterino2` (Chatterino and Chatterino7) and from a Moltorino install. Portable mode, enabled by a `portable` file next to `Alpharino.exe`, still keeps data beside the executable.

One-time import from an existing Chatterino or Moltorino profile is not built yet. Both of those apps keep their settings under `%APPDATA%\Chatterino2\Settings` in this tree's previous layout, so a later import can copy that directory into `%APPDATA%\Alpharino` when the Alpharino settings file is still absent.

### Issues

Report crashes and other problems in [GitHub issues](https://github.com/AlphaEid3/Alpharino/issues).

### Attribution

Alpharino is licensed under the MIT License. Original copyright notices from Chatterino 2, Chatterino7, and Moltorino stay in the source files and in `LICENSE`. AlphaEid3 is added alongside those notices.

- [Moltorino](https://github.com/EEVA-inc/Moltorino) (EEVA-inc / MoltoBenne)
- [Chatterino7](https://github.com/SevenTV/chatterino7) (SevenTV)
- [Chatterino 2](https://github.com/Chatterino/chatterino2)

The Chatterino 2 wiki is [here](https://wiki.chatterino.com).

### Screenshots from Chatterino7

![Example of Personal Emotes](https://user-images.githubusercontent.com/27637025/227032811-837c56eb-7724-431b-b00e-b944c9289dff.png)
![Example of Paints](https://user-images.githubusercontent.com/27637025/227034147-cb1fcd76-dbae-4878-9551-96ffa64dd1a9.png)

### AVIF Support

When building Alpharino, you might not have access to a static build of `libavif`. In that case, you can define `CHATTERINO_NO_AVIF_PLUGIN` in CMake. If you have `qavif.so` from [kimageformats](https://invent.kde.org/frameworks/kimageformats) installed on your system, the application will pick it up and use AVIF images.

## Building

To get the source code with the required submodules:

```shell
git clone --recurse-submodules https://github.com/AlphaEid3/Alpharino.git
```

or

```shell
git clone https://github.com/AlphaEid3/Alpharino.git
cd Alpharino
git submodule update --init --recursive
```

- [Building on Windows](BUILDING_ON_WINDOWS.md)
- [Building on Windows with vcpkg](BUILDING_ON_WINDOWS_WITH_VCPKG.md)
- [Building on Linux](BUILDING_ON_LINUX.md) (reference; not a CI target)
- [Building on macOS](BUILDING_ON_MAC.md) (reference; not a CI target)
- [Building on FreeBSD](BUILDING_ON_FREEBSD.md) (reference; not a CI target)

The Windows executable is `Alpharino.exe`.

## Git blame

This project has big commits in the history which touch most files while only doing stylistic changes. To improve the output of git-blame, consider setting:

```shell
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

This will ignore all revisions mentioned in the [`.git-blame-ignore-revs` file](./.git-blame-ignore-revs). GitHub does this by default.

## Code style

The code is formatted using [clang-format](https://clang.llvm.org/docs/ClangFormat.html). Our configuration is found in the [.clang-format](.clang-format) file in the repository root directory.

For more contribution guidelines, take a look at [the wiki](https://wiki.chatterino.com/Contributing%20for%20Developers/).
