```
  _        __ _     _ _
 (_)_ __  / _(_) __| | |
 | | '_ \| |_| |/ _` | |
 | | | | |  _| | (_| | |
 |_|_| |_|_| |_|\__,_|_|  2.5
```

[![Release](https://img.shields.io/github/v/release/ManOfInfinity/infidl)](https://github.com/ManOfInfinity/infidl/releases)
[![Build](https://img.shields.io/github/actions/workflow/status/ManOfInfinity/infidl/build.yml)](https://github.com/ManOfInfinity/infidl/actions)
[![License: AGPL](https://img.shields.io/badge/License-AGPL-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)

A fast, multi-platform command-line downloader optimized for speed, based on libcurl.

**infidl** splits a download into fixed-sized chunks and downloads them in-order with multiple concurrent connections. It natively supports HTTPS proxies, HTTP/2, resume, and parallel chunked downloads.

## Demo

```
$ infidl -c8 --auto-size 1 "https://example.com/ubuntu-24.04-desktop-amd64.iso"
 [=========================>-- -- -- -- -- -- -- -- ] 67.4% | 4.12 GiB / 6.11 GiB | 85.30 MiB/s | ETA 24s
```

```
$ infidl -c16 --auto-size 1 --show-details "https://cdn.example.com/movie.mkv"
URL: https://cdn.example.com/movie.mkv
Content-Type: application/octet-stream
Saving To: movie.mkv
File Size: 17.21GiB
Chunks: 16*1.07GiB + 1*1.06GiB
 [==================================================] 100.0% | 17.21 GiB / 17.21 GiB | 152.45 MiB/s | 1m55s
Download Finished.
```

## Features

- Multi-connection parallel downloads with configurable chunk sizes
- HTTP/HTTPS/SOCKS proxy support (including HTTPS CONNECT tunneling)
- Resume interrupted downloads
- HTTP/2 support with automatic fallback
- Mirror/fallback URL support
- Modern progress bar with real-time speed and ETA
- Cross-platform: Linux, macOS, Windows

## Quick Start

```bash
# Basic download with 8 connections
infidl -c8 "https://example.com/file.bin"

# Download with auto chunk sizing
infidl -c16 --auto-size 1 "https://example.com/large.bin"

# Resume an interrupted download
infidl -c8 --resume "https://example.com/large.bin"

# Download through a proxy
infidl -c8 --proxy socks5://127.0.0.1:1080 "https://example.com/file.bin"

# HTTPS tunnel proxy
infidl -c8 --tunnel-proxy https://proxy:8080 "https://example.com/file.bin"

# Save to specific directory and filename
infidl -c8 -D /downloads -o myfile.bin "https://example.com/file.bin"

# Show detailed info (URL, content-type, file size, chunks)
infidl -c8 --show-details "https://example.com/file.bin"
```

Run `infidl -h` for all available options.

## Installation

### Pre-built Binaries

Download from the [releases](https://github.com/ManOfInfinity/infidl/releases) page:

| Platform | File | Notes |
|----------|------|-------|
| Linux (static) | `infidl-linux-x86_64-static.tar.gz` | Zero dependencies, runs anywhere |
| Linux (dynamic) | `infidl-linux-x86_64.tar.gz` | Requires libcurl, libevent |
| macOS (ARM64) | `infidl-macos-arm64.tar.gz` | Requires libcurl, libevent (`brew install curl libevent`) |
| Windows (x64) | `infidl-windows-x86_64.zip` | Includes all required DLLs and CA certs |

### Build from Source

#### Dependencies

- **Runtime**: [libcurl](https://curl.se/libcurl/) >= 7.55, [libevent](https://libevent.org/) >= 2.0.20
- **Build**: GCC or Clang, Python 3 (for waf build system)
- **Optional**: git (version info), asciidoc + docbook-xsl (man page)

#### Build

```bash
./waf configure --prefix=/usr
./waf build
./waf install
```

To build without the man page:

```bash
./waf configure --disable-man
./waf build
```

## Credits

Originally created by [Mohammad AlSaleh](https://github.com/saldl/saldl) (2014-2016).
Now maintained by [ManOfInfinity](https://github.com/ManOfInfinity).

## License

[GNU Affero General Public License (AGPL)](https://www.gnu.org/licenses/agpl-3.0.html)

## Issues

Report bugs at [github.com/ManOfInfinity/infidl/issues](https://github.com/ManOfInfinity/infidl/issues)
