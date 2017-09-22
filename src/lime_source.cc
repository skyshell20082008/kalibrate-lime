/*
 * Copyright (c) 2010, Joshua Lackey
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *     *  Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *
 *     *  Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <string.h>
#include <pthread.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <complex>

#include "lime_source.h"
#include "convenience.h"
extern int g_verbosity;


#define DEFAULT_SAMPLE_RATE		24000
#define DEFAULT_BUF_LENGTH		(1 * 16384)
#define MAXIMUM_OVERSAMPLE		16
#define MAXIMUM_BUF_LENGTH		(MAXIMUM_OVERSAMPLE * DEFAULT_BUF_LENGTH)
#define BUFFER_DUMP				4096

#define FREQUENCIES_LIMIT		1000

#ifdef _WIN32
inline double
round (double x)
{
  return floor (x + 0.5);
}
#endif

int lime_source::lime_source_init()
{
    m_samples_per_buffer = MAXIMUM_BUF_LENGTH/2; //fix for int16 storage
	m_elemsize = SoapySDR_formatToSize(SOAPY_SDR_CF32);
	mBuf = (complex *)malloc(m_samples_per_buffer * m_elemsize);
	memset(mBuf, 0, m_samples_per_buffer * m_elemsize);
	if (!mBuf) {
		perror("malloc");
		exit(1);
	}
}

void lime_source::lime_source_destroy()
{
    if(mBuf)
    {
        free(mBuf);
        mBuf=0;
    }
}

lime_source::lime_source (float sample_rate, long int fpga_master_clock_freq)
{

  m_fpga_master_clock_freq = fpga_master_clock_freq;
  m_desired_sample_rate = sample_rate;
  m_center_freq = 0.0;
  m_sample_rate = 0.0;
  m_decimation = 0;
  lime_source_init();
  m_cb = new circular_buffer (CB_LEN, sizeof (complex), 0);

  pthread_mutex_init (&m_u_mutex, 0);
}


lime_source::lime_source (unsigned int decimation,
			  long int fpga_master_clock_freq)
{

  m_fpga_master_clock_freq = fpga_master_clock_freq;
  m_center_freq = 0.0;
  m_sample_rate = 0.0;
  lime_source_init();
  m_cb = new circular_buffer (CB_LEN, sizeof (complex), 0);

  pthread_mutex_init (&m_u_mutex, 0);

  m_decimation = decimation & ~1;
  if (m_decimation < 4)
    m_decimation = 4;
  if (m_decimation > 256)
    m_decimation = 256;
}


lime_source::~lime_source ()
{

  stop ();
  delete m_cb;
  SoapySDRDevice_deactivateStream(dev, stream, 0, 0);
  SoapySDRDevice_closeStream(dev, stream);
  SoapySDRDevice_unmake(dev); 
  //verbose_close (dev);
  lime_source_destroy();
  pthread_mutex_destroy (&m_u_mutex);
}


void
lime_source::stop ()
{

  pthread_mutex_lock (&m_u_mutex);

  int result;
  result = SoapySDRDevice_deactivateStream(dev, stream, 0, 0);
  if (result != 0)
    {
      printf ("Soapy_stop_rx() failed: %s (%d)\n",
	      SoapySDRDevice_lastError(), result);
      pthread_mutex_unlock (&m_u_mutex);
      exit (1);
    }
  pthread_mutex_unlock (&m_u_mutex);
}


void
lime_source::start ()
{

  pthread_mutex_lock (&m_u_mutex);

  int result;
  result = SoapySDRDevice_activateStream(dev, stream, 0, 0, 0);
  //result = verbose_start_rx (dev, verbose_rx_callback, this);
  if (result != 0)
    {
      printf ("Soapy_start_rx() failed: %s (%d)\n",
	      SoapySDRDevice_lastError(), result);
      pthread_mutex_unlock (&m_u_mutex);
      exit (1);
    }
  pthread_mutex_unlock (&m_u_mutex);
}


void
lime_source::calculate_decimation ()
{

  float decimation_f;

//      decimation_f = (float)m_u_rx->fpga_master_clock_freq() / m_desired_sample_rate;
  m_decimation = (unsigned int) round (decimation_f) & ~1;

  if (m_decimation < 4)
    m_decimation = 4;
  if (m_decimation > 256)
    m_decimation = 256;
}


float
lime_source::sample_rate ()
{

  return m_sample_rate;

}


int
lime_source::tune (double freq)
{

  int r = 0;
  
  pthread_mutex_lock (&m_u_mutex);
  if (freq != m_center_freq)
    {

      r = verbose_set_frequency (dev, (uint64_t) freq);
      //if (g_verbosity)
	//printf ("verbose_set_freq: %d\n", int (freq));

      if (r < 0)
	fprintf (stderr, "Tuning failed!\n");
      else
	m_center_freq = freq;
    }

  pthread_mutex_unlock (&m_u_mutex);

  return 1;			//(r < 0) ? 0 : 1;
}

int
lime_source::set_freq_correction (int ppm)
{
  m_freq_corr = ppm;
  return verbose_ppm_set(dev, ppm);
  //return 0;			// TODO: add support for ppm correction
}

bool
lime_source::set_antenna (int antenna)
{
  int r;
  r = (int)SoapySDRDevice_setAntenna(dev, SOAPY_SDR_RX, 0, "LNAL");
  if (r != 0) {
    fprintf(stderr, "WARNING: Failed to set antenna!!!.\n");
  }
  return (r < 0) ? 0 : 1;
}

bool
lime_source::set_gain (int tia_gain, int lna_gain, int pga_gain)
{
  int r = 0;

  if (lna_gain > 30)
    lna_gain = 30;
  if (pga_gain > 19)
    pga_gain = 19;
  if(tia_gain > 12)
      tia_gain = 12;
  if (g_verbosity)
    printf ("limerf: set gain %d/%d/%d\n", lna_gain, tia_gain, pga_gain);

  if (tia_gain)
   // r = verbose_set_amp_enable (dev, amp_gain);
   r = (int)SoapySDRDevice_setGainElement(dev, SOAPY_SDR_RX, 0, "TIA", tia_gain);  
  if (pga_gain)
  //  r |= verbose_set_vga_gain (dev, vga_gain);
  r=(int)SoapySDRDevice_setGainElement(dev, SOAPY_SDR_RX, 0, "PGA", pga_gain);
  if (lna_gain)
  //  r |= verbose_set_lna_gain (dev, lna_gain);
  r = (int)SoapySDRDevice_setGainElement(dev, SOAPY_SDR_RX, 0, "LNA", lna_gain);
  return (r < 0) ? 0 : 1;
}


/*
 * open() should be called before multiple threads access lime_source.
 */
int
lime_source::open (unsigned int subdev)
{
  int i, r, device_count, count;
  uint32_t dev_index = subdev;
  uint32_t samp_rate;
 char *dev_query = "driver=lime,soapy=0";
  samp_rate = m_fpga_master_clock_freq; // from constructor
  m_sample_rate = 1000000;
   

  if (g_verbosity)
    printf ("verbose_init()\n");
  SoapySDR_setLogLevel(SOAPY_SDR_FATAL);
  if (g_verbosity)
    printf ("verbose_open()\n");
  //r = verbose_open (&dev);
  r = verbose_device_search(dev_query, &dev, &stream, SOAPY_SDR_CF32);
  if (r <0)
    {
      fprintf (stderr, "Failed to open limerf device.\n");
      exit (1);
    }

  /* Set the sample rate */
  r = verbose_set_sample_rate (dev, samp_rate);
  if (g_verbosity)
    printf ("verbose_set_sample_rate(%u)\n", samp_rate);
  if (r < 0)
    fprintf (stderr, "WARNING: Failed to set sample rate.\n");
  //r = verbose_set_baseband_filter_bandwidth (dev, 2500000);
  r = verbose_set_bandwidth(dev,10000000);
  if (r != 0)
    {
      printf ("verbose_baseband_filter_bandwidth_set() failed: \n");
      return EXIT_FAILURE;
    }
   /* Reset endpoint before we start reading from it (mandatory) */
   //r = verbose_reset_buffer(dev);
   //if (r < 0)
	//fprintf(stderr, "WARNING: Failed to reset buffers.\n");
  return 0;
}

#define USB_PACKET_SIZE		(2 * 16384)
#define FLUSH_SIZE		512


int
lime_source::fill (unsigned int num_samples, unsigned int *overrun_i)
{

 	//unsigned char ubuf[USB_PACKET_SIZE];
	unsigned int i, j, space, overruns = 0;
	complex *c;
	int n_read;
    int r;
	int flags = 0;
	long long timeNs = 0;
	long timeoutNs = 1000000;
    
    //printf ("begin fill %d -- %d",num_samples,overrun_i);
	while((m_cb->data_available() < num_samples) && (m_cb->space_available() > 0)) {
         void *buffs[] = {mBuf};
		// read one usb packet from the usrp
		pthread_mutex_lock(&m_u_mutex);
                //fix_me change to soapy stream
        r = SoapySDRDevice_readStream(dev, stream, buffs, m_samples_per_buffer, &                                                   flags, &timeNs, timeoutNs);
		//if (verbose_read_sync(dev, ubuf, sizeof(ubuf), &n_read) < 0) {
       if(r < 0){
			pthread_mutex_unlock(&m_u_mutex);
			fprintf(stderr, "error: soapy_standard_rx::read\n");
			return -1;
		}
        n_read = r;
		pthread_mutex_unlock(&m_u_mutex);

		// write complex<short> input to complex<float> output
        
		c = (complex *)m_cb->poke(&space);

		// set space to number of complex items to copy
		space = n_read / 2;

		// write data
		for(i = 0, j = 0; i < space; i += 1, j += 2)
			//c[i] = complex((ubuf[j] - 127) * 256, (ubuf[j + 1] - 127) * 256);
            //c[i] = complex (mBuf[j]*256, mBuf[j + 1]*256);
            c[i] = mBuf[i];
		// update cb
		m_cb->wrote(i);
	}

	// if the cb is full, we left behind data from the usb packet
	if(m_cb->space_available() == 0) {
		fprintf(stderr, "warning: local overrun\n");
		overruns++;
	}

	if(overrun_i)
		*overrun_i = overruns;

	return 0;
}



/*
 * Don't hold a lock on this and use the usrp at the same time.
 */
circular_buffer *
lime_source::get_buffer ()
{

  return m_cb;
}


int
lime_source::flush (unsigned int flush_count)
{

  m_cb->flush ();
  verbose_reset_buffer(dev);
  //fill(flush_count * FLUSH_SIZE, 0);
  m_cb->flush ();

  return 0;
}
