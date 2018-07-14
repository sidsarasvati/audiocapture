#include "soundio/soundio.h"

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <future>
#include <vector>

using namespace std;


// TODO - use ring buffer w/o wrapper
struct RecordContext {
    struct SoundIoRingBuffer *ring_buffer;
};

static enum SoundIoFormat prioritized_formats[] = {
    SoundIoFormatFloat32NE,
    SoundIoFormatFloat32FE,
    SoundIoFormatS32NE,
    SoundIoFormatS32FE,
    SoundIoFormatS24NE,
    SoundIoFormatS24FE,
    SoundIoFormatS16NE,
    SoundIoFormatS16FE,
    SoundIoFormatFloat64NE,
    SoundIoFormatFloat64FE,
    SoundIoFormatU32NE,
    SoundIoFormatU32FE,
    SoundIoFormatU24NE,
    SoundIoFormatU24FE,
    SoundIoFormatU16NE,
    SoundIoFormatU16FE,
    SoundIoFormatS8,
    SoundIoFormatU8,
    SoundIoFormatInvalid,
};

static int prioritized_sample_rates[] = {
    48000,
    44100,
    96000,
    24000,
    0,
};

static int min_int(int a, int b) {
    return (a < b) ? a : b;
}

static int usage(char *exe) {
    fprintf(stderr, "Usage: %s [options]\n"
            "Options:\n"
            "  [--listdevices]\n"
            "  [--record {--deviceid $deviceid}] # uses default input device if --deviceid is not passed]\n"
            "  [--recordconv --deviceid-ch0 $deviceid-ch0 --deviceid-ch1 $deviceid-ch1]\n"
            "  [--verbose]\n", exe);
    return 1;
}

static void print_channel_layout(const struct SoundIoChannelLayout *layout) {
    if (layout->name) {
        fprintf(stderr, "%s", layout->name);
    } else {
        fprintf(stderr, "%s", soundio_get_channel_name(layout->channels[0]));
        for (int i = 1; i < layout->channel_count; i += 1) {
            fprintf(stderr, ", %s", soundio_get_channel_name(layout->channels[i]));
        }
    }
}

static void print_device(struct SoundIoDevice *device, bool is_default, bool verbose) {
    const char *default_str = is_default ? " (default)" : "";
    const char *raw_str = device->is_raw ? " (raw)" : "";
    fprintf(stderr, "%s%s%s\n", device->name, default_str, raw_str);
    if (!verbose) {
        return;
    }

    fprintf(stderr, "  id: %s\n", device->id);
    if (device->probe_error) {
        fprintf(stderr, "  probe error: %s\n", soundio_strerror(device->probe_error));
    } else {
        fprintf(stderr, "  channel layouts:\n");
        for (int i = 0; i < device->layout_count; i += 1) {
            fprintf(stderr, "    ");
            print_channel_layout(&device->layouts[i]);
            fprintf(stderr, "\n");
        }
        if (device->current_layout.channel_count > 0) {
            fprintf(stderr, "  current layout: ");
            print_channel_layout(&device->current_layout);
            fprintf(stderr, "\n");
        }
        fprintf(stderr, "  sample rates:\n");
        for (int i = 0; i < device->sample_rate_count; i += 1) {
            struct SoundIoSampleRateRange *range = &device->sample_rates[i];
            fprintf(stderr, "    %d - %d\n", range->min, range->max);
        }
        if (device->sample_rate_current)
            fprintf(stderr, "  current sample rate: %d\n", device->sample_rate_current);
        fprintf(stderr, "  formats: ");
        for (int i = 0; i < device->format_count; i += 1) {
            const char *comma = (i == device->format_count - 1) ? "" : ", ";
            fprintf(stderr, "%s%s", soundio_format_string(device->formats[i]), comma);
        }
        fprintf(stderr, "\n");
        if (device->current_format != SoundIoFormatInvalid)
            fprintf(stderr, "  current format: %s\n", soundio_format_string(device->current_format));
        fprintf(stderr, "  min software latency: %0.8f sec\n", device->software_latency_min);
        fprintf(stderr, "  max software latency: %0.8f sec\n", device->software_latency_max);
        if (device->software_latency_current != 0.0)
            fprintf(stderr, "  current software latency: %0.8f sec\n", device->software_latency_current);
    }
    fprintf(stderr, "\n");
}

static void read_callback(struct SoundIoInStream *instream, int frame_count_min, int frame_count_max) {
    struct RecordContext *rc = (RecordContext*) instream->userdata;
    struct SoundIoChannelArea *areas;
    int err;
    char *write_ptr = soundio_ring_buffer_write_ptr(rc->ring_buffer);
    int free_bytes = soundio_ring_buffer_free_count(rc->ring_buffer);
    int free_count = free_bytes / instream->bytes_per_frame;
    if (free_count < frame_count_min) {
        fprintf(stderr, "ring buffer overflow\n");
        exit(1);
    }
    int write_frames = min_int(free_count, frame_count_max);
    int frames_left = write_frames;
    for (;;) {
        int frame_count = frames_left;
        if ((err = soundio_instream_begin_read(instream, &areas, &frame_count))) {
            fprintf(stderr, "begin read error: %s", soundio_strerror(err));
            exit(1);
        }
        if (!frame_count)
            break;
        if (!areas) {
            // Due to an overflow there is a hole. Fill the ring buffer with
            // silence for the size of the hole.
            memset(write_ptr, 0, frame_count * instream->bytes_per_frame);
        } else {
            for (int frame = 0; frame < frame_count; frame += 1) {
                for (int ch = 0; ch < instream->layout.channel_count; ch += 1) {
                    memcpy(write_ptr, areas[ch].ptr, instream->bytes_per_sample);
                    areas[ch].ptr += areas[ch].step;
                    write_ptr += instream->bytes_per_sample;
                }
            }
        }
        if ((err = soundio_instream_end_read(instream))) {
            fprintf(stderr, "end read error: %s", soundio_strerror(err));
            exit(1);
        }
        frames_left -= frame_count;
        if (frames_left <= 0)
            break;
    }
    int advance_bytes = write_frames * instream->bytes_per_frame;
    soundio_ring_buffer_advance_write_ptr(rc->ring_buffer, advance_bytes);
}

static void overflow_callback(struct SoundIoInStream *instream) {
    static int count = 0;
    cerr << "overflow "  << ++count << endl;
}

static int list_devices(struct SoundIo *soundio, bool verbose = false) {
    int output_count = soundio_output_device_count(soundio);
    int input_count = soundio_input_device_count(soundio);
    int default_output = soundio_default_output_device_index(soundio);
    int default_input = soundio_default_input_device_index(soundio);
    fprintf(stderr, "--------Input Devices--------\n\n");
    for (int i = 0; i < input_count; i += 1) {
        struct SoundIoDevice *device = soundio_get_input_device(soundio, i);
        print_device(device, default_input == i, verbose);
        soundio_device_unref(device);
    }
    fprintf(stderr, "\n--------Output Devices--------\n\n");
    for (int i = 0; i < output_count; i += 1) {
        struct SoundIoDevice *device = soundio_get_output_device(soundio, i);
        print_device(device, default_output == i, verbose);
        soundio_device_unref(device);
    }
    fprintf(stderr, "\n%d devices found\n", input_count + output_count);
    return 0;
}

int record_in(SoundIo* soundio, char* device_id = nullptr) {
    const int RING_BUFFER_DURATION_SECONDS = 30;
    int ret = 0;
    int capacity = 0;
    struct RecordContext rc;
    struct SoundIoDevice* input_device = nullptr;
    int sample_rate = 0;
    SoundIoFormat fmt = SoundIoFormatInvalid;
    FILE* out_f = nullptr;
    struct SoundIoInStream *instream = nullptr;

    // get input device
    if (device_id) {
        for (int i = 0; i < soundio_input_device_count(soundio); i += 1) {
            struct SoundIoDevice *device = soundio_get_input_device(soundio, i);
            if (strcmp(device->id, device_id) == 0) {
                input_device = device;
                break;
            }
            soundio_device_unref(device);
        }
        if (!input_device) {
            cerr << "Input Device '" << device_id << "' not available" << endl;
            goto finally;
        }
    } else {
        int device_index = soundio_default_input_device_index(soundio);
        input_device = soundio_get_input_device(soundio, device_index);
        if (!input_device) {
            cerr << "No Input Device Available" << endl;
            ret = 1;
            goto finally;
        }
    }

    cerr << "Input Device: " << input_device->name << endl;

    if (input_device->probe_error) {
        cerr << "Unable to probe device: "<< soundio_strerror(input_device->probe_error) << endl;
        return ret;
    }

    soundio_device_sort_channel_layouts(input_device);

    sample_rate = input_device->sample_rates[0].max;
    fmt = input_device->formats[0];

    out_f = fopen(string(string("/tmp/recordconv-") + input_device->name + ".raw").c_str(), "wb");
    if (!out_f) {
        cerr << "unable to open file: " << strerror(errno) << endl;
        ret = 1;
        goto finally;
    }

    instream = soundio_instream_create(input_device);
    if (!instream) {
        cerr <<  "out of memory" << endl;
        ret = 1;
        goto finally;
    }
    instream->format = fmt;
    instream->sample_rate = sample_rate;
    instream->read_callback = read_callback;
    instream->overflow_callback = overflow_callback;
    instream->userdata = &rc;

    if ((ret = soundio_instream_open(instream))) {
        cerr <<  "unable to open input stream: " << soundio_strerror(ret) << endl;
        goto finally;
    }

    cerr << instream->layout.name << " " << sample_rate << "Hz "
         << soundio_format_string(fmt) << " interleaved " << endl;


    capacity = RING_BUFFER_DURATION_SECONDS * instream->sample_rate * instream->bytes_per_frame;
    rc.ring_buffer = soundio_ring_buffer_create(soundio, capacity);
    if (!rc.ring_buffer) {
        cerr << "out of memory" << endl;
        ret = 1;
        goto finally;
    }

    if ((ret = soundio_instream_start(instream))) {
        cerr << "unable to start input device: " << soundio_strerror(ret) << endl;
         goto finally;
    }

    cout << "Recording... " << endl;

    // Read from ring_buffer and write to file
    while (true) {
        soundio_flush_events(soundio);
        sleep(1);
        int fill_bytes = soundio_ring_buffer_fill_count(rc.ring_buffer);
        char *read_buf = soundio_ring_buffer_read_ptr(rc.ring_buffer);
        size_t amt = fwrite(read_buf, 1, fill_bytes, out_f);
        if ((int)amt != fill_bytes) {
            fprintf(stderr, "write error: %s\n", strerror(errno));
            return 1;
        }
        soundio_ring_buffer_advance_read_ptr(rc.ring_buffer, fill_bytes);
    }

finally:
    soundio_instream_destroy(instream);
    soundio_device_unref(input_device);
    return ret;
}

int main(int argc, char **argv) {
    char* exe = argv[0];
    bool listdevices = false;
    bool verbose = false;
    bool record = false;
    bool recordconv = false;
    char* deviceid_in = nullptr;
    char* deviceid_ch0 = nullptr;
    char* deviceid_ch1 = nullptr;

    // sidster: this cmd line argument parsing code is way too clever
    // a.k.a annoying a.k.a complex; handle with care
    for (int i = 1; i < argc; i++) {
        char* arg = argv[i];
        if (arg[0] == '-' && arg[1] == '-') {
            if (strcmp(arg, "--listdevices") == 0) {
                listdevices = true;
            } else if (strcmp(arg, "--record") == 0) {
                record = true;
                // find if the optional '--deviceid $deviceid'
                // argument was passed
                if(argc >= i+2 && strcmp(argv[++i], "--deviceid") == 0) {
                    deviceid_in = argv[++i];
                }
            } else if (strcmp(arg, "--recordconv") == 0) {
                recordconv = true;
                i++;
                // find if the required arguments '--deviceid-ch0
                // $deviceid --deviceid-ch1 $deviceid' were passed
                if (argc < i+4) {
                    return usage(exe);
                }
                for (int j=0; j < 2; j++) {
                    if (strcmp(argv[i], "--deviceid-ch0") == 0) {
                        deviceid_ch0 = argv[++i];
                        i++;
                    } else if (strcmp(argv[i], "--deviceid-ch1") == 0) {
                        deviceid_ch1 = argv[++i];
                        i++;
                    } else {
                        return usage(exe);
                    }
                }
            } else if (strcmp(arg, "--verbose") == 0) {
                verbose = true;
            } else {
                return usage(exe);
            }
        } else {
            return usage(exe);
        }
    }

    int ret = 0;

    // -----------
    // SETUP
    struct SoundIo *soundio = soundio_create();
    if (!soundio) {
        fprintf(stderr, "out of memory\n");
        ret = 1;
        goto finally;
    }
    // Connects to backend in the following order -
    // 1. JACK
    // 2. PulseAudio
    // 3. ALSA (Linux)
    // 4. CoreAudio (OSX)
    // 5. WASAPI (Windows)
    // 6. Dummy
    //
    // Alternatively, we can use soundio_connect_backend() to explicitly choose a backend to connect to.
    if ((ret = soundio_connect(soundio))) {
        fprintf(stderr, "error connecting: %s", soundio_strerror(ret));
        ret = 1;
        goto finally;
    }
    soundio_flush_events(soundio);
    // ------------

    // LIST DEVICES
    if (listdevices) {
        ret = list_devices(soundio, verbose);
        goto finally;
    }

    // RECORD
    if (record) {
        ret = record_in(soundio, deviceid_in);
        goto finally;
    }

    // RECORD CONV
    if (recordconv) {
        vector<future<int>> record_tasks;
        record_tasks.push_back(async(launch::async, record_in, soundio, deviceid_ch0));
        record_tasks.push_back(async(launch::async, record_in, soundio, deviceid_ch1));

        // wait for all tasks to finish
        for (auto& t : record_tasks) {
            t.get();
        }
        
        goto finally;
    }


finally:
    if (soundio) {
        soundio_destroy(soundio);
    }
    return ret;
}
