/*
 sdr.c
 
 */

#include <stdlib.h>
#include <math.h>
#include <complex.h>
#include <fftw3.h>
#include <string.h>

#include "filter.h"
#include "sdr.h"

static gint blk_pos=0;
static int n;

sdr_data_t *sdr_new() {
    // create an SDR, and initialise it
    sdr_data_t *sdr;
    
    sdr = malloc(sizeof(sdr_data_t));
    sdr->loVector = 1;  // start the local oscillator
    //sdr->loPhase = 1;   // this value is bogus but we're going to set the frequency anyway
    sdr->loPhase = cexp(I);
    sdr->agc_gain = 0;   // start off as quiet as possible
    sdr->mode = SDR_LSB;
    sdr->agc_speed = 0.005;
    
}

void sdr_destroy(sdr_data_t *sdr) {
    if (sdr) {
        free(sdr);
    }
}

int sdr_process(sdr_data_t *sdr) {
    // actually do the SDR bit
    int i, j, k;
    double y, accI, accQ;
    complex c;
    fft_data_t *fft = sdr->fft;
    int size = sdr->size;
    
    float agc_gain = sdr->agc_gain;
    float agc_peak = 0;

    // remove DC with a highpass filter
    for (i = 0; i < size; i++) {       // DC removal; R.G. Lyons page 553
                c = sdr->iqSample[i] + sdr->dc_remove * 0.95;
                sdr->iqSample[i] = c - sdr->dc_remove;
                sdr->dc_remove = c;
    }

    // copy this period to FFT buffer
    // FIXME - needs to deal with cases where size isn't a multiple of FFT_SIZE
    memmove(fft->samples, fft->samples+size, sizeof(complex)*(FFT_SIZE-size));
    memmove(fft->samples+FFT_SIZE-size, sdr->iqSample, sizeof(complex)*size);

    // shift frequency
    for (i = 0; i < sdr->size; i++) {
		sdr->iqSample[i] *= sdr->loVector;
    	sdr->loVector *= sdr->loPhase;
	}
	
    filter_fir_process(sdr->filter, sdr->iqSample);

    // apply some AGC here
    for (i = 0; i < sdr->size; i++) {
        y = cabs(sdr->iqSample[i]);
        if (agc_peak < y) agc_peak = y;

    }

    if (agc_peak == 0) agc_peak = 0.00001;    // don't be zero, in case we have digital silence
    y = agc_peak * agc_gain;  // y is the peak level scaled by the current gain

    if (y <= 1) {       // Current level is below the soundcard max, increase gain
        agc_gain += (1/ agc_peak - agc_gain) * sdr->agc_speed;
    } else {                   // decrease gain
        agc_gain += (1 / agc_peak - agc_gain);
    }
    y = agc_gain * 0.5; // change volume
    for (i = 0; i < sdr->size; i++){
        sdr->iqSample[i] *= y;
    }
    
    sdr->agc_gain = agc_gain;

    switch(sdr->mode) {
        case SDR_LSB:
            for (i=0; i < sdr->size; i++) {
	            y = creal(sdr->iqSample[i])+cimag(sdr->iqSample[i]);
                sdr->output[i] = y;
            }
            break;
        case SDR_USB:
            for (i=0; i < sdr->size; i++) {
	            y = creal(sdr->iqSample[i])-cimag(sdr->iqSample[i]);
                sdr->output[i] = y;
            }
            break;
    }  
}

void fft_setup(sdr_data_t *sdr) {
    sdr->fft = (fft_data_t *)malloc(sizeof(fft_data_t));
    sdr->fft_size = FFT_SIZE;
    fft_data_t *fft = sdr->fft;
    
    fft->samples = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * sdr->fft_size);
    fft->out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * sdr->fft_size);
    fft->plan = fftw_plan_dft_1d(sdr->fft_size, fft->samples, fft->out, FFTW_FORWARD, FFTW_ESTIMATE);
	fft->status = EMPTY;
	fft->index = 0;
}

void fft_teardown(sdr_data_t *sdr) {
    fft_data_t *fft = sdr->fft;
    fftw_destroy_plan(fft->plan);
    fftw_free(fft->samples);
    fftw_free(fft->out);
    free(sdr->fft);
}

