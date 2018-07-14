#include "soundio/soundio.h"

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <future>
#include <vector>
#include <chrono>
#include <thread>
#include <deque>
#include <signal.h>

#include "dsp/resampler.h++"

#define PCM_MAX_DOUBLE 32768.0

void wave_write_header(FILE * wf, int16_t channel_count) {

    int16_t pcm = 0x01;
    int32_t t;
    int16_t s;

    int32_t bitrate = 16;
    int32_t sample_rate = 8000;
    int32_t file_size = 0x0FFFFFFF + 1;
    int32_t data_size = file_size - 44 + 8;

    #define fswrite(stream,str) fwrite(str,sizeof(char),strlen(str),stream)
    #define fwrite32(stream,pl) fwrite(pl,sizeof(int32_t),1,stream)
    #define fwrite16(stream,ps) fwrite(ps,sizeof(int16_t),1,stream)

    fswrite (wf,"RIFF");
    fwrite32(wf,&file_size);
    fswrite (wf,"WAVE");
    fswrite (wf,"fmt ");
    fwrite32(wf,&bitrate); // subchunk size
    fwrite16(wf,&pcm);
    fwrite16(wf,&channel_count);
    fwrite32(wf,&sample_rate);
    t = (channel_count*sample_rate*bitrate)>>3; // bitrate
    fwrite32(wf,&t);
    s=2; // block align (bytes)
    fwrite16(wf,&s);
    s=16; // bits per sample
    fwrite16(wf,&s);
    fswrite (wf,"data");
    fwrite32(wf,&data_size);

}

// TODO - use ring buffer w/o wrapper
struct RecordContext {
    struct SoundIoRingBuffer *ring_buffer;
};

/*

./audiocapture \
  --deviceid-ch0 AppleHDAEngineInput:1F,3,0,1,0:1 \
  --deviceid-ch1 com.rogueamoeba.Loopback:EC187ACD-4D85-480C-963D-4DF1D9B1F865 | \
  python3 -m cogito.tools.graph_compute dialog.yml | \
  python3 -m cogito.tools.events

*/

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
            "  [--verbose]\n"
            "  --deviceid-ch0 device_id\n"
            "  --deviceid-ch1 device_id\n"
            "  [--out out.wav]\n", exe);
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

    if (!verbose) {
        fprintf(stderr, "%s\t(%s)\n", device->name, device->id);
        return;
    }

    fprintf(stderr, "%s%s%s\n", device->name, default_str, raw_str);

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
    std::cerr << "overflow "  << ++count << std::endl;
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

int record_in(
    SoundIo* soundio,
    char* device_id,
    std::mutex* channel_mutex,
    std::deque<int16_t>* channel_deque)
{
    const int RING_BUFFER_DURATION_SECONDS = 30;
    int ret = 0;
    int capacity = 0;
    struct RecordContext rc;
    struct SoundIoDevice* input_device = nullptr;
    int sample_rate = 0;
    SoundIoFormat fmt = SoundIoFormatInvalid;
    struct SoundIoInStream *instream = nullptr;
    double padding = 500.0;
    double Fs_in = 44100;
    double Fs_out = 8000.0;
    double* sample_buffer = nullptr;
    double v;
    int i, j;
    int16_t n, vi, *ptr;
    size_t n_bytes_written;

    Resampler resampler;
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
            std::cerr << "Input Device '" << device_id << "' not available" << std::endl;
            goto finally;
        }
    } else {
        int device_index = soundio_default_input_device_index(soundio);
        input_device = soundio_get_input_device(soundio, device_index);
        if (!input_device) {
            std::cerr << "No Input Device Available" << std::endl;
            ret = 1;
            goto finally;
        }
    }

    if (input_device->probe_error) {
        std::cerr << "Unable to probe device: "<< soundio_strerror(input_device->probe_error) << std::endl;
        return ret;
    }

    soundio_device_sort_channel_layouts(input_device);

    sample_rate = input_device->sample_rates[0].max;
    fmt = input_device->formats[0];

    instream = soundio_instream_create(input_device);
    if (!instream) {
        std::cerr <<  "out of memory" << std::endl;
        ret = 1;
        goto finally;
    }
    instream->format = fmt;
    instream->sample_rate = sample_rate;
    instream->read_callback = read_callback;
    instream->overflow_callback = overflow_callback;
    instream->userdata = &rc;

    if ((ret = soundio_instream_open(instream))) {
        std::cerr <<  "unable to open input stream: " << soundio_strerror(ret) << std::endl;
        goto finally;
    }

    //std::cerr << instream->layout.name << " " << sample_rate << "Hz "
    //     << soundio_format_string(fmt) << " interleaved " << std::endl;

    capacity = RING_BUFFER_DURATION_SECONDS * instream->sample_rate * instream->bytes_per_frame;
    rc.ring_buffer = soundio_ring_buffer_create(soundio, capacity);
    if (!rc.ring_buffer) {
        std::cerr << "out of memory" << std::endl;
        ret = 1;
        goto finally;
    }

    if ((ret = soundio_instream_start(instream))) {
        std::cerr << "unable to start input device: " << soundio_strerror(ret) << std::endl;
         goto finally;
    }

    Fs_in = static_cast<double>(sample_rate);
    resampler.init(Fs_in, Fs_out, padding);

    fprintf(stderr, "Recording... %s (%s) Fs_in: %.0f Hz Fs_out: %.0f Hz\n",
        input_device->name, instream->layout.name, Fs_in, Fs_out);

    sample_buffer = (double*) malloc(sizeof(double) * resampler.max_output());

    // Read from ring_buffer and write to file
    while (true) {
        soundio_flush_events(soundio);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        int fill_bytes = soundio_ring_buffer_fill_count(rc.ring_buffer);

        if (fill_bytes==0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }
        char *read_buf = soundio_ring_buffer_read_ptr(rc.ring_buffer);

        ptr = reinterpret_cast<int16_t*>(read_buf);

        {
            std::lock_guard<std::mutex> guard(*channel_mutex);

            // TODO: this assume stereo audio, if mono, no need to avg values
            n_bytes_written = 0;
            for (i=0; i<fill_bytes/2; i+=2) {
                v = ( (static_cast<double>(ptr[i]) / PCM_MAX_DOUBLE) +
                      (static_cast<double>(ptr[i+1]) / PCM_MAX_DOUBLE) ) / 2.0;
                n = resampler.insert(v, sample_buffer);
                for (j=0; j < n; j++) {
                    // write to a deque instead
                    vi = static_cast<int16_t>(sample_buffer[j] * PCM_MAX_DOUBLE);
                    channel_deque->push_back(vi);
                    n_bytes_written++;
                }
            }
        }

        soundio_ring_buffer_advance_read_ptr(rc.ring_buffer, fill_bytes);
    }

finally:
    soundio_instream_destroy(instream);
    soundio_device_unref(input_device);
    if (sample_buffer != nullptr) {
        free(nullptr);
    }
    return ret;
}


int record_main(
    SoundIo* soundio,
    std::vector<char*> channel_ids,
    FILE* fout)
{
    std::vector<std::future<int>> channel_tasks;

    std::deque<int16_t> deque_ch0;
    std::deque<int16_t> deque_ch1;
    std::vector<std::deque<int16_t>*> channel_deque;
    std::mutex channel_mutex;

    channel_deque = {&deque_ch0, &deque_ch1};
    int n_available;

    int16_t vi;

    for (int i=0; i < channel_ids.size(); i++) {

        channel_tasks.push_back(async(
            std::launch::async,
            record_in,
            soundio,
            channel_ids[i],
            &channel_mutex,
            channel_deque[i]));
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        {
            std::lock_guard<std::mutex> guard(channel_mutex);

            // synchronize the available bytes across all queues
            n_available = 16000; // Fs_out * 2
            for (int i=0; i < channel_ids.size(); i++) {
                if (channel_deque[i]->size() < n_available) {
                    n_available = channel_deque[i]->size();
                }
            }
            if (n_available == 0) {
                continue;
            }

            // interleave bytes from all queues

            for (int i=0; i < n_available; i++) {
                for (int j=0; j < channel_ids.size(); j++) {

                    vi = channel_deque[j]->front();
                    if (fwrite( &vi, sizeof(int16_t), 1, fout)!=1) {
                        fprintf(stderr, "write error: %s\n", strerror(errno));
                        return 1;
                    }
                    channel_deque[j]->pop_front();
                }
            }
            fflush(fout);
        }
    }

    for (auto& t : channel_tasks) {
        t.get();
    }
}

void signal_handler(int signal) {
    fprintf(stderr, "Caught signal %d\n", signal);
    exit(1);
}

int main(int argc, char **argv) {
    char* exe = argv[0];
    bool listdevices = false;
    bool verbose = false;
    char* deviceid_ch0 = nullptr;
    char* deviceid_ch1 = nullptr;
    char* output_name = (char*) "-";
    int ret;
    FILE* fout;
    int16_t n_channels = 0;
    struct sigaction sigIntHandler;

    std::vector<char*> channel_ids;

    sigIntHandler.sa_handler = signal_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;

    sigaction(SIGINT, &sigIntHandler, NULL);
    sigaction(SIGTERM, &sigIntHandler, NULL);

    for (int i = 1; i < argc; i++) {
        char* arg = argv[i];

        if (strcmp(arg, "--listdevices") == 0) {
            listdevices = true;
            continue;

        } else if (strcmp(arg, "--verbose") == 0) {
            verbose = true;
            continue;
        } else if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            return usage(exe);
        }

        // parameters which consume a value
        if (argc > i+1) {
            if (strcmp(arg, "--out") == 0) {
                output_name = argv[++i];
            } else if (deviceid_ch0 == nullptr && strcmp(arg, "--deviceid-ch0") == 0) {
                deviceid_ch0 = argv[++i];
                n_channels++;
                std::cerr << "ch0: " << deviceid_ch0 << std::endl;
            } else if (deviceid_ch1 == nullptr && strcmp(arg, "--deviceid-ch1") == 0) {
                deviceid_ch1 = argv[++i];
                n_channels++;
                std::cerr << "ch1: " << deviceid_ch1 << std::endl;
            } else {
                std::cerr << "unrecognized option: " << arg << std::endl;
                return usage(exe);
            }
        } else {
            std::cerr << "unrecognized option: " << arg << std::endl;
            return usage(exe);
        }
    }

    ret = 0;

    // -----------
    // SETUP
    struct SoundIo *soundio = soundio_create();
    if (!soundio) {
        std::cerr << "out of memory\n" << std::endl;
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

    if (n_channels==0) {
        std::cerr << "no device id configured for capture" << std::endl;
        goto finally;
    }

    if (deviceid_ch0 == nullptr) {
        std::cerr << "channel 0 device id not set" << std::endl;
        goto finally;
    }

    channel_ids = {deviceid_ch0, deviceid_ch1};

    if (strcmp(output_name, "-") == 0) {

        fout = stdout;

    } else {
        fout = fopen(output_name, "wb");

        if (!fout) {
            std::cerr << "unable to open file: " << strerror(errno) << std::endl;
            ret = 1;
            goto finally;
        }

    }

    wave_write_header(fout, n_channels);

    record_main(soundio, channel_ids, fout);

    fclose(fout);

finally:
    if (soundio) {
        soundio_destroy(soundio);
    }
    return ret;
}
