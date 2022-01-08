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

https://user-images.githubusercontent.com/8294550/147177606-a184eece-5e16-4a58-9ffb-e1e8d8d76265.mp4
<p align="center">
<a href="https://www.youtube.com/watch?v=XtA-Jh8XU_c" target="_blank">
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
  
  -d <dev> Enable HW accel for a given device (like vaapi or vdpau)

  -h This help

Note:
  Please note that some options depends on the screen resolution. If I'm unable
	to get the resolution and the -r parameter is not set:
  - If X11 (wallpaper) mode: The video will always fill the screen area
  - If Windowed mode: Window will be the same size as the video
```

## Performance analysis
Here are a series of executions on an i5 7300HQ (integrated video), with different
resolutions, FPS and with and without hardware acceleration for
[this](https://pixabay.com/videos/lake-sunset-trees-leaves-japan-91562/) video:

### 1440p
| Resolution        | HW Accel | FPS | CPU Usage (%): | GPU Usage (%): | Time (User/Sys/Elapsed): |
|-------------------|----------|-----|----------------|----------------|--------------------------|
| 2560x1440 | NO       | 60  | 65.5%          | 31.43%         | 10.38s, 0.43s, 16.51s    |
| 2560x1440 | YES      | 60  | 32.76%         | 36.96%         | 4.67s, 0.72s, 16.48s     |
| 2560x1440 | NO       | 30  | 35.76%         | 15.43%         | 5.69s, 0.23s, 16.61s     |
| 2560x1440 | YES      | 30  | 16.56%         | 18.26%         | 2.31s, 0.41s, 16.50s     |

### 1080p
| Resolution        | HW Accel | FPS | CPU Usage (%): | GPU Usage (%): | Time (User/Sys/Elapsed): |
|-------------------|----------|-----|----------------|----------------|--------------------------|
| 1920x1080 | NO       | 60  | 40.46%         | 30.06%         | 6.4s, 0.31s, 16.58s      |
| 1920x1080 | YES      | 60  | 21.26%         | 31.36%         | 2.89s, 0.60s, 16.47s     |
| 1920x1080 | NO       | 30  | 23.05%         | 15.13%         | 3.59s, 0.19s, 16.49s     |
| 1920x1080 | YES      | 30  | 10.76%         | 15.36%         | 1.46s, 0.31s, 16.49s     |

### 720p
| Resolution      | HW Accel | FPS | CPU Usage (%): | GPU Usage (%): | Time (User/Sys/Elapsed): |
|-----------------|----------|-----|----------------|----------------|--------------------------|
| 1280x720 |    NO    |  60 |       24%      |     29.13%     |   3.72s, 0.25s, 16.58s   |
| 1280x720 |    YES   |  60 |     15.06%     |     25.56%     |   1.93s, 0.53s, 16.47s   |
| 1280x720 |    NO    |  30 |     13.36%     |      14.5%     |   2.04s, 0.15s, 16.48s   |
| 1280x720 |    YES   |  30 |      8.16%     |     12.86%     |   1.07s, 0.26s, 16.48s   |

It can be observed that CPU usage decreases dramatically as resolution and FPS decrease. Also note
that using hardware acceleration (`-d` parameter) halves CPU usage, so its use is highly recommended.

**Note 1**: For each resolution/fps pair, the tests were repeated three times and the average was
obtained. These tests can be run with `bench/bench.sh`)

**Note 2**: Please note that the CPU time reported by `intel_gpu_time` is the CPU time of all cores.
As Anipaper's actual CPU usage is generally distributed evenly across the cores/threads, the
consumption of each core is more-or-less /num_cores, i.e: 8% usage means approximately 2%
per core on a quadcore system.

**Note 3**: All results obtained above were executed without pauses. Executions with pauses are
expected to have much lower total CPU usage. (more on that below)

### Pause support
To further decrease CPU usage, Anipaper has a 'pause' mode: whenever the total area of visible
windows (considering possible overlap) is greater than a configurable threshold (default 70%)
the video playback pauses. This means that Anipaper will pause whenever a program is full screen
or even if there are too many windows covering enough of the wallpaper.

Considering a 'normal' usage where most windows occupy the entire screen (or most of it), Anipaper
would run as little time as possible, and would not take over of the CPU.

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

### Custom builds
Anipaper's pause support allows two types of customization: screen area (default 70%),
and window check interval (100ms). Both can be configured via `SCREEN_AREA_THRESHOLD`
and `CHECK_PAUSE_MS` macros:
```bash
# Set screen area threshold to 90%
CFLAGS="-DSCREEN_AREA_THRESHOLD=90" make

# Set window area polling to 200ms
CFLAGS="-DCHECK_PAUSE_MS=200" make

# Set both
CFLAGS="-DSCREEN_AREA_THRESHOLD=90 -DCHECK_PAUSE_MS=200" make
```
or via `anipaper.h`.

## Contributing
Anipaper is always open to the community and willing to accept contributions,
whether with issues, documentation, testing, new features, bugfixes, typos, and
etc. Welcome aboard.

## License and Authors
Anipaper is licensed under MIT License. Written by Davidson Francis and
(hopefully) other
[contributors](https://github.com/Theldus/anipaper/graphs/contributors).
