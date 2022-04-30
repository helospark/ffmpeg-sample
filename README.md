# FFmpeg-sample

FFMpeg samples/playground to play with performance and HW acceleration, to be used to make Tactview faster.

## Compile & run

Compile with

    ./compile.sh

You need libavcodec installed.

Run VAAPI hardware accelerated version:

    ./hwdecode.out vaapi ~/Videos/sample.mp4

You may need to change what device is used in the code, change "/dev/dri/renderD129" to other devices if needed.

It is possible that sometimes one GPU is much slower with HW accelerated VAAPI, for
 example on my laptop VAAPI on Intel GPU is about 1.5x faster than multithreaded software implementation,
 while AMD GPU is about 0.1x the speed of software decoding. What GPU is used depends on the device above.

You can check what driver points to what device with:

    # ls -l /sys/class/drm/renderD*/device/driver


Run multithreaded version with:

    ./swdecode.out vaapi ~/Videos/sample.mp4

## Check GPU usage

While checking multithreaded decoding is easy by checking CPU usage, checking GPU requires custom program.

On Intel, use intel-gpu-top program, then run

    sudo intel_gpu_top

It should show GPU usage, the GPU decoding percentage is visible under "Video/0", if you see as 0%, you are not actually decoding on intel GPU.
