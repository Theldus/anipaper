# Anipaper documentation

## Introduction

This file is intended to document, at a high level, how Anipaper works. I do
this for 3 reasons:

**a)** this project can serve as a 'working example' (though no audio support)
of the amazing
'[An ffmpeg and SDL Tutorial](http://dranger.com/ffmpeg/tutorial01.html)',
and I think it's interesting to show how it works in general.

**b)** for myself, to understand after a few months =).

**c)** the more texts about how libav works the better, right? sharing knowledge
is always good =).

I also need to make it clear that this is my first contact with the FFmpeg
libraries and part of what I programmed or understood could be wrong, feel free
to send PRs or issues to improve this document.

### Notes
1) This document only gives an overview of the process, for implementation
details please see the anipaper source code as well as:
['An ffmpeg and SDL Tutorial'](http://dranger.com/ffmpeg/tutorial01.html)
(outdated, but the general idea persists) and
[ffmpeg-libav-tutorial](https://github.com/leandromoreira/ffmpeg-libav-tutorial),
which is an excellent guide (mainly Chapter 0).

2) This doc refers to the anipaper commit
[`a084db589d1201b60f176219ce9a40b6bc4a89aa`](https://github.com/Theldus/anipaper/tree/a084db589d1201b60f176219ce9a40b6bc4a89aa), please use this as the source code
reference.

3) Anipaper and this document refer to version 59 of libavcodec. Things may
change in future majors and I can't guarantee that this will stay up to date. If
you like, use the following commit hash as a reference to the exact version of
FFmpeg used by me:
[`3a9861e22c636d843c10e23f5585196d1f3400dd`](https://github.com/FFmpeg/FFmpeg/tree/3a9861e22c636d843c10e23f5585196d1f3400dd).

## How it works?
A video file is usually a container, which contains a stream of videos, audios,
subtitles, and so on. Each stream is encoded with a certain codec -- an 'algorithm'
that dictates how that stream is organized internally -- and that stream is
divided in packets (discussed later).

### Opening the input file
To decode a video, we need to open the file
([`avformat_open_input()`](https://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#gac05d61a2b492ae3985c658f34622c19d)),
get the stream information
([`avformat_find_stream_info()`](https://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#gad42172e27cddafb81096939783b157bb))
and iterate over each stream until we find the desired one (in the case of
video, `AVMEDIA_TYPE_VIDEO`).

When found, we look for the decoder for that stream's codec
([avcodec_find_decoder()](https://ffmpeg.org/doxygen/trunk/group__lavc__core.html#ga51e35d01da2b3833b3afa839212c58fa)), and
initialize the 'codec_context' (lines [905 to 913](https://github.com/Theldus/anipaper/blob/a084db589d1201b60f176219ce9a40b6bc4a89aa/anipaper.c#L905-L914)).

After all that, we are ready to decode. All of this is done in Anipaper's
[init_av()](https://github.com/Theldus/anipaper/blob/a084db589d1201b60f176219ce9a40b6bc4a89aa/anipaper.c#L860) function.

### Reading packets and decoding
All that's left now is reading the packets and decoding them, the pseudo-code
(taken from [1]) illustrates the general process:
```text
10 OPEN video_stream FROM video.avi (we already done that in the previous section)
20 READ packet FROM video_stream INTO frame
30 IF frame NOT COMPLETE GOTO 20
40 DO SOMETHING WITH frame
50 GOTO 20
```
The reading of packets is done by the
[`av_read_frame()`](https://ffmpeg.org/doxygen/trunk/group__lavf__decoding.html#ga4fdb3084415a82e3810de6ee60e46a61)
routine (the name seems to be misleading, right?). However, it is important to
point out that 'packet' here means a 'piece' of information: it could be an
audio stream packet (which you will probably ignore), another video stream...
and even if it is the desired stream, it doesn't imply a frame: it can contains
a full frame (type I), or a 'partial' frame (B and P, relax, you don't have to
worry about that).

Therefore, associated with `av_read_frame()`, there is also the packet decode,
made by the pair of functions
[`avcodec_send_packet()`](https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga58bc4bf1e0ac59e27362597e467efff3) (which sends the
packet to the decoder) and
[`avcodec_receive_frame()`](https://ffmpeg.org/doxygen/trunk/group__lavc__decoding.html#ga11e6542c4e66d3028668788a1a74217c)
(which decodes a packet and
returns a frame, which *may not* be the full frame).

It is important to emphasize the importance of using `avcodec_send_packet()`
and `avcodec_receive_frame()` together. It is also important to note that if
`avcodec_receive_frame()` was not been able to produce a complete frame so far
(if it returns `AVERROR(EAGAIN)`), you should continue reading new packets
with `av_read_frame()`.

Once you have a complete frame (`av_receive_frame` returns 0), you can decide
what to do with it:
[save it to file](https://github.com/Theldus/anipaper/blob/a084db589d1201b60f176219ce9a40b6bc4a89aa/util.c#L42), display it on screen,
convert it, and so on. It is important to note that the frame image format may
not correspond to the classic RGB, usually video files use YUV420 (this is not
guaranteed either, you can check the format with my_frame->format), you can
convert them to RGB using the
[`sws_scale`](https://ffmpeg.org/doxygen/trunk/group__libsws.html#gae531c9754c9205d90ad6800015046d74) routine, or work directly
with their original format (SDL supports YUV420).

A more detailed algorithm could be:

```c
while (av_codec_receive_frame() >= 0)
  if (avcodec_send_packet() < 0)
    abort();

  ret = avcodec_receive_frame();
  if (ret == AVERROR(EAGAIN)) /* not complete frame yet. */
    continue;

  else if (ret < 0)
    abort();

  save_frame_or_whatever()
```

Okay, but what about the screen? and FPS? let's go to the next section.

### Synchronizing everything
So far we already have some idea on how to extract the frames from the video and
maybe save it to a file (or display it on screen), but how to do it at the right
time? We cannot display the frames as soon as we get it, as we would be
displaying the video very fast, we cannot take too long, otherwise the video
would be slow... how to solve this?

The answer to this is threads: we don't know how many packets we need to read
until we have a frame that can be displayed and we still need to respect the
video 'fps'.

Therefore, the main anipaper's organization is as follows:

- a) A first thread extracts packets and queues them in a linked list. If the
list is full, the thread is blocked waiting for a new slot, if empty, the thread
fills it. This thread will be called 'enqueue_packets'.

- b) A second thread reads the queued packets from the first thread (a) and
decodes them. The decoded frames are placed in a second linked list, already in
`SDL_Texture` format and ready to be displayed on the screen. In the absence of
packets, the thread also remains blocked. This is the 'decode_packets' thread.

- c) A third thread (the main one), from *time to time* removes a frame/picture
from the linked list produced by (b) and displays it on the screen. After that,
go back to sleep again. If there are no frames available, the thread is blocked.

All the magic is up to the third thread: it needs to know how long to sleep
between each frame. How do we know this? PTS.

#### PTS
PTS (or Presentation Time Stamp) is a value present in each frame and specifies
*when* the frame should be displayed on the screen. PTS alone doesn't mean much,
so there's also 'time_base' for that. The time_base represents the base unit of
measure of a frame, in seconds. Therefore, the real time that a frame should be
displayed is the time_base multiplied by the PTS value.

An example video follows below:
```text
== time_base: 0.000065 ==
pts:    0 / time_when_should_be_displayed: 0
pts:  512 / time_when_should_be_displayed: 512  * 0.000065 = 0.03328 (or 33ms)
pts: 1024 / time_when_should_be_displayed: 1024 * 0.000065 = 0.06656 (or 66ms)
pts: 1536 / time_when_should_be_displayed: 1536 * 0.000065 = 0.09984 (or 99.84ms)
...
```
So the right way to do things is to use PTS, not FPS. But this raises another
question: how do we know the PTS of the next frame? we **predict**!.

We start with a generic delay value of 40ms (or 25 fps), this is our first 'PTS'.
The first frame is then displayed 40ms after the execution of the program and we
note its PTS and delay, the second frame we calculate the difference of the PTS
from the current frame to the previous one, and if valid, this is our delay. We
can then write down the current PTS value, the current delay (for future
calculations) and we're ready to sleep, right? Wrong.

The delay we got from subtracting the PTS is a 'generic' delay, a delay that
assumes things always work as they should, in a perfect, hypothetical world.

We need to actually calculate our *true* delay: to do this, we 'simulate' the
running time that the program would be in the future (predicted current time +
delay) and subtract from the actual current time. With that, we have the time
our program _really_ should sleep. In case this time is very small (say, less
than 10ms), we can consider that we are late and ignore that frame, we do the
famous 'frame dropping' =).

Are you confused? we can summarize all this in a routine called
[`adjust_timers()`](https://github.com/Theldus/anipaper/blob/a084db589d1201b60f176219ce9a40b6bc4a89aa/anipaper.c#L596),
its pseudo_code could be something like:

```c
frame_last_delay = 0.04; /* or 40ms. */
frame_last_pts   = 0;
frame_timer      = current_time_in_secs();

adjust_timers:
  delay = current_frame_pts - frame_last_pts;

  /*
   * negative delay occurs if we do not known the last
   * pts, such as the first execution.
   *
   * delay >= 1 implies delay greater than 1 second
   */
  if (delay <= 0 || delay >= 1.0)
    delay = frame_last_delay;

  /* save values. */
  frame_last_delay = delay;
  frame_last_pts = pts;

  /* calculate true delay. */
  frame_timer += delay /* our predicted timer in the future. */
  true_delay = frame_timer - current_time_in_secs();

  return true_delay;
```

It makes sense? Now that we know how much we can sleep, the code for thread c)
becomes intuitive:
```c
refresh_screen:

  /* sleep if no picture available. */
  picture = get_picture_from_queue();
  if (picture == null)
    abort() /* no more pictures. */

  true_delay = adjust_timers(picture.pts);

  /* if we're late, skip frame. */
  if (true_delay < 0.010)
     goto refresh_screen();

  update_screen(picture);

  sleep_ms(true_delay);

  goto refresh_screen;
```
The following figure illustrates the general behavior of the anipaper. Threads
a), b) and c) are represented in green, yellow and blue, respectively:

<p align="center">
<img align="center" src="https://i.imgur.com/vE8f83J.png" alt="anipaper overview">
<br>
<i>Anipaper overview</i>
</p>

---

And that's it, I hope this overview can help others get started with FFmpeg
libraries and also understand how Anipaper works internally.
