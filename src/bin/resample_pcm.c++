

#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <vector>
#include <cstdint>

#include "dsp/resampler.h++"

// ffmpeg -i "/Users/nsetzer/Music/Library/Beast/Beast/05_Mr._Hurricane.mp3" -ac 1 -ar 44100 -f s16le -acodec pcm_s16le input.pcm
// vlc --demux=rawaud --rawaud-channels 1 --rawaud-samplerate 44100 input.pcm

#define PCM_MAX_DOUBLE 32768.0
#define STREAM_BUFFER_SIZE 2048


void write_header(FILE * wf) {

    int16_t pcm = 0x01;
    int32_t t;
    int16_t s;

    int32_t bitrate = 16;
    int16_t channel_count = 1;
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
    fwrite32(wf,&bitrate);
    fwrite16(wf,&pcm);
    fwrite16(wf,&channel_count);
    fwrite32(wf,&sample_rate);
    t = (channel_count*sample_rate*bitrate)>>3;
    fwrite32(wf,&t);
    s=2;
    fwrite16(wf,&s);
    s=16;
    fwrite16(wf,&s);
    fswrite (wf,"data");
    fwrite32(wf,&data_size);

}
void resample(double Fs_in, double Fs_out, FILE* fin, FILE* fout)
{
    // set Fc = Fs_out - padding
    double padding = 1000.0;
    Resampler resampler(Fs_in, Fs_out, padding);

    int n, i, j;
    int16_t vi;
    double* sample_buffer = (double*) malloc(sizeof(double) * resampler.max_output());
    char buffer[STREAM_BUFFER_SIZE];
    int16_t* ptr;
    size_t bytes_read;

    // expect to read signed PCM data
    ptr = reinterpret_cast<int16_t*>(&buffer[0]);
    bytes_read = fread(buffer, sizeof(char), sizeof(buffer), fin);

    while (bytes_read > 0) {

        //std::cerr << "bytes read " << bytes_read << std::endl;

        for (i=0; i < bytes_read/2; i++) {
            double v = static_cast<double>(ptr[i]) / PCM_MAX_DOUBLE;
            n = resampler.insert(v, sample_buffer);

            for (j=0; j < n; j++) {
                vi = static_cast<int16_t>(sample_buffer[j] * PCM_MAX_DOUBLE);
                fwrite( &vi, sizeof(int16_t), 1, fout);
            }

        }

        fflush(fout);

        bytes_read = fread(buffer, sizeof(char), sizeof(buffer), fin);
    }

}

int main(int argc, char* argv[]) {

    // INPUT is assumed to be MONO RAW PCM
    // output is a wave file

    FILE* fin = stdin;
    FILE* fout = stdout;

    if (argc >= 2) {
        if (strcmp(argv[1], "-") != 0) {
            fin = fopen(argv[1], "rb");
        }
    }

    if (fin==NULL) {
        std::cerr << "unable to open input file" << std::endl;
        goto err;
    }

    if (argc >= 3) {
        if (strcmp(argv[2], "-") != 0) {
            fout = fopen(argv[2], "wb");
        }
    }

    if (fout==NULL) {
        std::cerr << "unable to open output file" << std::endl;
        goto err;
    }

    write_header(fout);
    resample(44100, 8000, fin, fout);

err:
    fclose(fin);
    fclose(fout);

}