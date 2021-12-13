# anipaper 📽
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)

A simple X11+SDL2 animated wallpaper setter and video player

## Introduction
Anipaper (ANImated Wallpaper) is a simple 'wallpaper setter' for X11
environments that use the root window to display videos in a loop.

Built  on top of the FFmpeg libraries (`libavcodec` and companions),
Anipaper paper strives to be as simple as possible, having only the basics,
thus ensuring a readable and highly modifiable source code.  Furthermore,
since it uses SDL2, it has a good performance without consuming all system
resources, depending on the type of video to be played.

<p align="center">
<a href="https://www.youtube.com/watch?v=XtA-Jh8XU_c" target="_blank">
<img align="center" src="https://i.imgur.com/Ye3uPyG.jpg" alt="Anipaper example">
<br>
Anipaper demo, click to open on YouTube
</a></br>
Background video by Qika Nugroho from <a href="https://pixabay.com/videos/lake-sunset-trees-leaves-japan-91562/">Pixabay</a>
</p>

## Features
Anipaper tries to be as simple as possible, so it doesn't try to be an all-in-one
solution or anything like that. However, there are some options on how the video
should be displayed, and also a window mode (-w), which makes it behave like any
other video player (no audio).

Its options/command-line arguments are as follows:
```text
Usage: anipaper <input-file>
  -o Execute only once, without loop (loop enabled by default)
  -w Enable windowed mode (do not set wallpaper)

Resolution options:
  -k (Keep) resolution, may appears smaller or bigger than the screen, preserve
     aspect ratio

  -s (Scale to) screen resolution, occupies the entire screen regardless of the
     aspect ratio!

  -f (Fit) to screen. Make the video fit into the screen (default)

  -r Set screen resolution, in format: WIDTHxHEIGHT

  -h This help

Note:
  Please note that some options depends on the screen resolution. If I'm unable
	to get the resolution and the -r parameter is not set:
  - If X11 (wallpaper) mode: The video will always fill the screen area
  - If Windowed mode: Window will be the same size as the video
```

## Performance
As a video player, resource usage is expected to be higher than a still-picture
wallpaper setter (no animation), and with Anipaper it's no different.

However, performance is similar to `ffplay`, `mplayer`, and others... and varies
a lot depending on codec, framerate, and video resolution. What I suggest is
testing these combinations and finding a balance that you like =).

## Known limitations
Incompatibility with compositors. Since compositors use X11's root window to manage
other windows, feature used by Anipaper. It is also clear that there is no Wayland
compatibility.

## Building/Installing
There is only two dependencies: SDL2 and FFmpeg libraries (`libavcodec`, `libavformat`,
among others).

However, it is worth noting that due to constant API change between major
versions of FFmpeg, it is recommended to use libavcodec version 59 (also successfully
tested on 58). If you want to be more accurate, use the following commit hash:
`3a9861e22c636d843c10e23f5585196d1f3400dd`.

A typical build on Ubuntu 18.04.5 would look something like:

### Dependencies
```bash
# Install SDL2
$ sudo apt install libsdl2-dev


# Install FFmpeg's libraries (nasm and pkg-config are FFmpeg build dependencies):
$ sudo apt install pkg-config nasm
$ wget https://github.com/FFmpeg/FFmpeg/archive/3a9861e22c636d843c10e23f5585196d1f3400dd.zip
$ unzip -q FFmpeg-3a9*.zip
$ rm FFmpeg-3a9*.zip
$ cd FFmpeg-3a9*/
$ ./configure
$ make -j$(nproc)
$ sudo make install
```

### Anipaper build
```bash
$ make

# Optionally (if you want to install):
$ make install # (PREFIX and DESTDIR allowed here, defaults to /usr/local/)
```

## Contributing
Anipaper is always open to the community and willing to accept contributions,
whether with issues, documentation, testing, new features, bugfixes, typos, and
etc. Welcome aboard.

## License and Authors
Anipaper is licensed under MIT License. Written by Davidson Francis and
(hopefully) other
[contributors](https://github.com/Theldus/anipaper/graphs/contributors).
