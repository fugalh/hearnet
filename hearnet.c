/**
 * @file hearnet.c
 *
 * "Play" your network.
 *
 * @author Hans Fugal
 */

#include <pthread.h>
#include <stdio.h>
#include <jack/jack.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pcap.h>
#include <unistd.h>

/* macros */
/// length of grain in samples
#define GRAIN_PERIOD 44
/// Our favorite irrational number
#define PI 3.14159265358979

/* data */
short grain_wavetable[GRAIN_PERIOD];
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int play_grain = 0;
jack_port_t *output_port;
jack_nframes_t srate;

/* functions */
/** initialize the grain wavetable */
int fill_grain_wavetable() /*{{{*/
{
    // read in the wavetable from grain.raw, which has mono signed 16bit samples
    // (i.e. the output of "sox grain.wav -r 44100 -s -w grain.raw")
    int fd;

    fd = open("grain.raw",O_RDONLY);
    if (fd < 0)
    {
	perror("Error opening \"grain.raw\"");
	return -1;
    }

    int count = 0;
    do
    {
	int retval;
	retval = read(fd,grain_wavetable, GRAIN_PERIOD-count);
	if (retval < 0)
	{
	    perror("Error reading \"grain.raw\"");
	    return -1;
	}
	if (retval == 0)
	{
	    fprintf(stderr,"\"grain.raw\" is too short. Expected %d samples.\n",
		    GRAIN_PERIOD);
	    return -1;
	}
	count += retval;
    } while (count < GRAIN_PERIOD);

    close(fd);

    return 0;
} 
/*}}}*/
/** process nframes frames (jack callback) */
int process (jack_nframes_t nframes, void *arg)/*{{{*/
{
    jack_default_audio_sample_t *out = (jack_default_audio_sample_t *) jack_port_get_buffer (output_port, nframes);

    // fill out with sizeof(jack_default_audio_sample_t)*nframes bytes
    int len = sizeof(jack_default_audio_sample_t)*nframes;

    pthread_mutex_lock(&mutex);
    int i;
    memset(out, 0, len);
    for (i=0; i<play_grain; i++)
    {
	int truncate;
	if (i*GRAIN_PERIOD > len)
	{
	    fprintf(stderr,"!");
	    break;
	}

	truncate = (i+1)*GRAIN_PERIOD - len;
	if (truncate < 0) truncate = 0;
	memcpy(out+(i*GRAIN_PERIOD), grain_wavetable, GRAIN_PERIOD - truncate);
    }
    play_grain = 0;
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
    play_grain++;
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
    
    // init grain wavetable
    if (fill_grain_wavetable() < 0)
	return 1;


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

    while(1)
	pcap_dispatch(hdl_pcap, 1, packet_handler, 0);

    return 0;
}/*}}}*/

// vim:sw=4:ts=8:tw=78:fdm=marker
