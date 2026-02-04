# ReelVault

A native GTK3 application for browsing and launching your local film collection. Requires a TMDB API key to fetch film details.

![ReelVault screenshot](screenshots/screenshot.png)

## Features

- **Poster Grid:** Browse your film collection with cover art scraped from TMDB
- **Metadata:** View cast, crew, ratings, plot summaries, and more
- **Filtering:** Filter by genre and year
- **Sorting:** Sort by title, year, rating, date added
- **External Player:** Launch films in your preferred video player
- **Manual Matching:** Fix incorrect matches or identify unrecognized films

## Installation

### Arch Linux (AUR)

- Package: https://aur.archlinux.org/packages/reelvault

Using an AUR helper:
```bash
yay -S reelvault
```

### Debian/Ubuntu (.deb)

Download the `.deb` from GitHub Releases, then install it:
```bash
sudo apt install ./reelvault_*.deb
```

### Fedora/RHEL (.rpm)

Download the `.rpm` from GitHub Releases, then install it:
```bash
sudo dnf install ./reelvault-*.rpm
```

## Running

```bash
reelvault
```

On first run, you'll be prompted to:
1. Enter your TMDB API key (get one free at https://www.themoviedb.org/settings/api)
2. Add your film library directories

The config file is stored here: `~/.config/reelvault/config.ini`

## Advanced Search

The search bar supports simple `key:value` tokens:

- `actor: Nick` or `cast:nick` (search cast/actors)
- `plot: kidnapping` (search plot/overview text)
- `title: dune` (search title explicitly)

You can combine tokens and plain text. Plain text searches the title.

Examples:
```text
actor:"Jason Momoa" dune
plot:"time travel"
title: alien
```

## Building From Source

### Dependencies

- GTK 3.0
- SQLite 3
- libcurl
- json-c
- libjpeg-turbo (libturbojpeg)

#### Arch Linux
```bash
sudo pacman -S base-devel gtk3 sqlite curl json-c libjpeg-turbo
```

#### Debian/Ubuntu
```bash
sudo apt install build-essential pkg-config libgtk-3-dev libsqlite3-dev libcurl4-openssl-dev libjson-c-dev libturbojpeg0-dev
```

### Build

```bash
make
```

### Run

```bash
./reelvault
```

## License

MIT

## Other Useful Projects

- A lightweight speech to text implementation [Auriscribe](https://github.com/rabfulton/Auriscribe)
- A full featured AI application [ChatGTK](https://github.com/rabfulton/ChatGTK)
- A Markdown notes application for your system tray [TrayMD](https://github.com/rabfulton/TrayMD)
- Try my AI panel plugin for XFCE [XFCE Ask](https://github.com/rabfulton/xfce-ask)
