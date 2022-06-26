# FFmpeg-sample

FFMpeg samples/playground to play with performance and HW acceleration, to be used to make Tactview faster.

It provides multiple implementation each doing the same operation:

 - Decode video
 - Scale video to 400x300
 - Saves one frame every 100 frames to /tmp
 - Measure FPS

# Performance

Decoding 4k h264 footage on my CPU i3-3120m (with iGPU HD 4000) using vaapi:

 - HW decode: 433 FPS
 - HW decode without filter: 105 FPS
 - SW decode: 54.9 FPS

## Compile & run

Compile with

    ./compile.sh

You need libavcodec and build-tools installed.

Run VAAPI hardware accelerated version:

    ./hwdecode.out ~/Videos/sample.mp4 "/dev/dri/renderD128"

hardware accelerated without the vaapi_scale filter (using software scaling):

    ./hwdecode_without_filter.out ~/Videos/sample.mp4 "/dev/dri/renderD128"

The "/dev/dri/renderD128" is the device used for decoding, usually it is the renderD128, however for multiple GPUs (example in a laptop) it may be different. For example my laptop uses renderD129 for Intel iGPU.

It is possible that sometimes one GPU is much slower with HW accelerated VAAPI, for
 example on my laptop VAAPI on Intel GPU is about 1.5x faster than multithreaded software implementation,
 while AMD GPU is about 0.1x the speed of software decoding. What GPU is used depends on the device above.

You can check what driver points to what device with:

    # ls -l /sys/class/drm/renderD*/device/driver


Run multithreaded version with:

    ./swdecode.out ~/Videos/sample.mp4

## Check GPU usage

While checking multithreaded decoding is easy by checking CPU usage, checking GPU requires custom program.

On Intel, use intel-gpu-top program, then run

    sudo intel_gpu_top

It should show GPU usage, the GPU decoding percentage is visible under "Video/0", if you see as 0%, you are not actually decoding on intel GPU.
