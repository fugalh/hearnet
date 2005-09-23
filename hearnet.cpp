/**
 * @file hearnet.c
 *
 * "Play" your network.
 *
 * @author Hans Fugal
 *
 * heavily modified by Leonard Ritter
 * 
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <jack/jack.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pcap.h>
#include <unistd.h>
#include <math.h>
#include <memory.h>
#include <time.h>

/* macros */
/// length of grain in samples
#define GRAIN_PERIOD 44
/// Our favorite irrational number
#define PI 3.14159265358979

#define MAX_VOICES 16

/* data */
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
jack_port_t *output_port;
jack_nframes_t srate;

struct voice
{
	bool active;
	float attack;
	float attacklength;
	float sinpos;
	float sinfreq;
	float amp;
	float decaylength;
	int age;
};

voice voices[MAX_VOICES];

void init_voices()
{
	memset(voices,0,sizeof(voices));	
}

voice* get_free_voice()
{
	for (int index = 0; index < MAX_VOICES; index++)
	{
		if (!voices[index].active)
			return &voices[index];
	}
	return 0;
}

float dbtoamp(float db)
{
	return pow(2,db/6);
}

/*}}}*/
/** process nframes frames (jack callback) */
int process (jack_nframes_t nframes, void *arg)/*{{{*/
{
    jack_default_audio_sample_t *out = (jack_default_audio_sample_t *) jack_port_get_buffer (output_port, nframes);

    pthread_mutex_lock(&mutex);

    memset(out, 0, sizeof(jack_default_audio_sample_t)*nframes);
	
	for (int index = 0; index != MAX_VOICES; index++)
	{
		if (voices[index].active)
		{
			voice* current_voice = &voices[index];
			float* pout = out;			
			for (int s = 0; s != nframes; s++)
			{
				*pout += sin(current_voice->sinpos) * current_voice->amp * current_voice->attack;
				if (*pout > 1.0f)
				{
					*pout = 1.0f;
				}
				else if (*pout < -1.0f)
				{
					*pout = -1.0f;
				}
				if (current_voice->attack < 1.0f)
				{
					current_voice->attack += 1.0f / ((float)srate * current_voice->attacklength);
				}
				else
				{
					current_voice->amp *= 1.0f - (1.0f / ((float)srate * current_voice->decaylength));
				}
				current_voice->sinpos += current_voice->sinfreq / (float)srate;
				if (current_voice->amp < 0.001)
				{
					current_voice->active = false;
				}
				pout++;
			}
		}
	}

    pthread_mutex_unlock(&mutex);

    return 0;
}/*}}}*/
/** jack callback to set sample rate */
int set_srate (jack_nframes_t nframes, void *arg)/*{{{*/
{
    printf("Setting srate to %luHz\n", nframes);
    srate = nframes;
    return 0;
}/*}}}*/
/** jack error callback */
void error (const char *desc)/*{{{*/
{
	fprintf (stderr, "JACK error: %s\n", desc);
}/*}}}*/
/** jack shutdown callback */
void shutdown(void *arg)/*{{{*/
{
    exit(1);
}/*}}}*/
/** packet handler called by pcap_dispatch in main() */
void packet_handler(u_char * args, const struct pcap_pkthdr *pcap_hdr, const u_char * p)/*{{{*/
{
    pthread_mutex_lock(&mutex);
	
	voice* new_voice = get_free_voice();
	if (new_voice)
	{
		memset(new_voice,0,sizeof(voice));
		new_voice->sinpos = 0.0f;
		float factor2 = (float)(rand()%5 + 1);
		//float factor = pow(2,(float)(rand()%12)*4.0f / 12.0f);
		float factor = pow(2,(float)(pcap_hdr->len / 256.0f)*3.0f / 12.0f) * factor2;
		new_voice->sinfreq = 55.0f * 2.0f * 3.14159f * factor + ((float)(rand()%5) * factor);
		new_voice->amp = 0.5 / (float)(MAX_VOICES); // if they all add up, we dont get bursts
		new_voice->decaylength = (rand()%100 + 100) / 1000.0f; 
		new_voice->attack = 0.0f;
		new_voice->attacklength = (rand()%20 + 1) / 1000.0f; // 10ms
		new_voice->active = true;
	}
    pthread_mutex_unlock(&mutex);
		
}/*}}}*/

void usage(void)/*{{{*/ 
{
    fprintf(stderr,
	    "\n"
	    "usage: hearnet [interface]\n"
	    "Default interface is eth0.\n"
	    );
    exit(1);
}/*}}}*/

/* main */
int main(int argc, char **argv)/*{{{*/
{
    int retval;
    char *dev = "eth0";
    char client_name[80];

    if (argc > 1)
	dev = argv[1];
    
    snprintf(client_name, sizeof(client_name), "hearnet %s", dev);
    // jack stuff {{{
    jack_client_t *client;
    const char **ports;
    jack_set_error_function(error);
    if ((client = jack_client_new(client_name)) == 0)
    {
	fprintf(stderr, "jack server not running?\n");
	return 1;
    }
    jack_set_process_callback(client,process,0);
    jack_set_sample_rate_callback(client,set_srate, 0);
    jack_on_shutdown(client, shutdown, 0);
    printf ("engine sample rate: %lu\n", jack_get_sample_rate(client));

    output_port = jack_port_register(client, "output", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    if (jack_activate(client)) 
    {
	fprintf(stderr, "cannot activate client\n");
	return 1;
    }

    if ((ports = jack_get_ports (client, NULL, NULL, JackPortIsPhysical|JackPortIsInput)) == NULL) {
	fprintf(stderr, "Cannot find any physical playback ports\n");
	exit(1);
    }
    int i=0;
    while (ports[i]!=NULL)
    {
	if (jack_connect(client, jack_port_name(output_port), ports[i]))
	    fprintf(stderr,"cannot connect output ports\n");
	i++;
    }
    free (ports);
    /*}}}*/
    
    // init (empty) voice table
	init_voices();


    // libpcap stuff /*{{{*/
    pcap_t *hdl_pcap;
    char perrbuf[PCAP_ERRBUF_SIZE];
    hdl_pcap = pcap_open_live(dev, BUFSIZ, 0, 0, perrbuf);
    if (hdl_pcap == NULL)
    {
	fprintf(stderr,"pcap_open_live; %s\n", perrbuf);
	usage();
    }
 
    /*}}}*/

	timeval tv_start;
	gettimeofday(&tv_start,0);
	srand(tv_start.tv_sec);
	while (1)
	{
		pcap_dispatch(hdl_pcap, 1, packet_handler, 0);
	}

    return 0;
}/*}}}*/

// vim:sw=4:ts=8:tw=78:fdm=marker
