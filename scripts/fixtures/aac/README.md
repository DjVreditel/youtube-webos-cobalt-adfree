# AAC decoder fixture

`stereo-48k.aac` is a generated 0.25-second, 1 kHz sine wave, AAC-LC, 48 kHz,
stereo, ADTS. It contains no captured YouTube content or user data. Generated with:

```sh
ffmpeg -f lavfi -i 'sine=frequency=1000:sample_rate=48000:duration=0.25' \
  -ac 2 -c:a aac -b:a 64k -f adts stereo-48k.aac
```

The real-library test exercises Cobalt's existing FDK initialization, packet
feed, decode and output-delay trimming methods twice with a decoder recreation.
It does not establish on-TV synchronization or live/DVR behavior.
