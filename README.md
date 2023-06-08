# MKVToolNix Mirror

This repository mirrors the official upstream repository on GitLab at [gitlab.com/mbunkus/mkvtoolnix](https://gitlab.com/mbunkus/mkvtoolnix).

## Intention

* Automatically mirror tags with source code
* Automatically create GitHub releases - so they can be subscribed to
* Automatically fill release notes

## Upstream Git

* Upstream: <https://gitlab.com/mbunkus/mkvtoolnix>
* Upstream MXE (build tools): <https://gitlab.com/mbunkus/mxe>

Single Branch: `main`

Release Tags: `release-*` (`77.0`, `75.0.0`)

## Build Windows

From `Building.for.Windows.md`:

* only supports cross compile from linux to windows; mingw on windows works but is not supported
* => ubuntu-latest
* `sudo apt-get install autopoint gperf libtool-bin lzip intltool python3-mako`
* // mkvtoolnix adjusted fork of mxe
* // build docs declare path must be $HOME/mxe
* `git clone https://gitlab.com/mbunkus/mxe $HOME/mxe`
* `./packaging/windows/setup_cross_compilation_env.sh`
* `rake`

setup sh script logs to `/tmp/mkvtoolnix_setup_cross_compilation_env.*` by default => LOGFILE to `/proc/$$/fd/1` for stdout

rake build

* `$HOME/mxe/tmp-qt6-qtbase-x86_64-pc-linux-gnu/qtbase-everywhere-src-6.4.3.build_/CMakeFiles/CMakeOutput.log`
* `$HOME/mxe/tmp-qt6-qtbase-x86_64-pc-linux-gnu/qtbase-everywhere-src-6.4.3.build_/CMakeFiles/CMakeError.log`
