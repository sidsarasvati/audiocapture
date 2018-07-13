
#include <cstdlib>
#include <cstdio>
#include <iostream>

#include "dsp/resampler.h++"

Resampler::Resampler(double Fs_in, double Fs_out, double Fs_padding) {

	// this requirement is some what arbitrary
	// however for low target rates this algorithm does not work very well.
	if (Fs_in<10.0||Fs_out<10.0) {
		// TODO THROW
	}

	m_x0 = m_x1 = 0.0;
	m_x = 1.0; // this removes any sample delay
	m_rate = Fs_in / Fs_out;

	if (Fs_out < Fs_in) {

		int num_poles_lpf = 6;
    	int percent_ripple = 15;

	    int coeff_buf_size;
	    double* tb = newCheby1Array(num_poles_lpf, &coeff_buf_size);
	    double* ta = newCheby1Array(num_poles_lpf, &coeff_buf_size);

	    double Fc = ((Fs_out/2.0)-Fs_padding)/ (Fs_in/2.0);
	    int num_coeff = cheby1(num_poles_lpf,percent_ripple, Fc, 0, tb, ta, coeff_buf_size);

		m_df2 = new DirectForm2Mono<double>(tb, ta, num_coeff);

		free(tb);
	    free(ta);
	}

	// these are used for a simple 1 pole LPF
	// double Fc = 7000.0;
	// double Fs = 44100.0;
	// double RC = 1.0/(Fc*2*3.14);
    // double dt = 1.0/Fs;
    // alpha = dt/(RC+dt);

}

/*
	out must be an array location where N consecutive double precision
	floats can can be written. N is determined by the resample rate.
*/
int Resampler::insert(double v, double * out) {
	int n=0;

	if (m_df2 != nullptr) {
		v = m_df2->IIR(v);
	}

	//out[0] = v;
	//return 1;

	/*
	// these are used for a simple 1 pole LPF, instead of the df2;
	out[0] = v_last + alpha * (v - v_last);
	v_last = v;
	return 1;
	*/


	m_x1 = m_x0;
	m_x0 = v;
	while (m_x <= 1.0) {
		*out++ = m_x1+(m_x0-m_x1)*m_x;
		m_x += m_rate;
		n++;
	}
	m_x -= 1.0;
	return n;

}

//returns the maximum number of samples an insert method could return.
//In practice, the filter may return less than this number frequently.
//A flush function may return up to twice this number.
int Resampler::max_output(void) const {
	int n = (int) (1./m_rate + .5);
	if ( n < 1 )
		n = 1;
	return n;
}

