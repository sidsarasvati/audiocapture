#ifndef SIGPROC_RESAMPLE_H
#define SIGPROC_RESAMPLE_H

#include "dsp/directform2.h++"
#include "dsp/cheby1.h++"

class Resampler
{
public:
    Resampler() {};
    Resampler(double FS_in,double FS_out, double Fs_padding, int num_poles = 6, int percent_ripple = 15);
    ~Resampler() {};

    void init(double FS_in,double FS_out, double Fs_padding, int num_poles = 6, int percent_ripple = 15);

    int insert(double value, double* out);
    int max_output(void) const;

private:
    double m_x0;
    double m_x1;
    double m_x;
    double m_rate;

    // these are used for a simple 1 pole LPF
    //double v_last = 0;
    //double alpha = 0;

    DirectForm2Mono<double> *m_df2 = nullptr;
};

#endif