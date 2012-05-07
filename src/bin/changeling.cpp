
// Changeling profanity delay
// James Harrison <james@talkunafraid.co.uk>
#include <csignal>
#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <sstream>
#include <string.h>
#include <sys/time.h> 
#include <time.h> 
#include <unistd.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <sndfile.hh>
#include <mosquitto.h>
#include "changeling.hpp"

using namespace std;

// Setup our JACK connectivity
/// Input ports
jack_port_t *input_port[2] = {NULL, NULL};
/// Output ports
jack_port_t *output_port[2] = {NULL, NULL};
/// JACK client instance
jack_client_t *client = NULL;
/// Null set of options to create the client with
jack_options_t jack_opt = JackNullOption;
/// Sample rate retrieved from the JACK server, set in main()
jack_nframes_t sample_rate;

// Our audio buffers
jack_ringbuffer_t *buffer_l;
jack_ringbuffer_t *buffer_r;

/// Run state - we start OUT
ChangelingRunState state = CHANGELING_STATE_STARTING;
ChangelingRunState last_state = CHANGELING_STATE_EXITING;

// Mosquitto handle
mosquitto *mqtt_client;

/// Our buffer file handle
SndfileHandle buffer_file;
// Where are we in playback of our buffer?
/// Buffer file playback index
int cur_playback_idx;

// How much delay do we have available?
/// Maximum delay in seconds
float max_delay_seconds;
/// Maximum delay in samples
jack_nframes_t max_delay_samples;
/// Current size of the buffer in samples
jack_nframes_t cur_delay_samples;


/// Interrupt handler - mark our state as EXITING so we can gracefully clear up
void int_handler(int x)
{
  cout << "Got interrupt, shutting down" << endl;
  // Set our state to exiting, let main() tear it all down
  state = CHANGELING_STATE_EXITING;
}
/// Error handler - called from JACK if JACK runs into issues.
void error (const char *desc)
{
  fprintf(stderr, "JACK error: %s\n", desc);
}
/// xrun handler - called from JACK if JACK drops samples
void xrun(void *arg)
{
  fprintf(stderr, "## WARNING - JACK xrun!\n");
}
/// JACK shutdown handler - called from JACK if JACK is shutting down so we can exit.
void jack_shutdown (void *arg)
{
  printf("JACK shutting down");
  exit (1);
}
/**
Processes audio from JACK.

The exact behaviour of this function depends on the ChangelingRunState we're in, but changeling will always put in as much audio as it takes out.

*/
int process (jack_nframes_t nframes, void *arg)
{
  // Grab pointers
  jack_default_audio_sample_t *out_l =  (jack_default_audio_sample_t *) jack_port_get_buffer (output_port[0], nframes);
  jack_default_audio_sample_t *out_r =  (jack_default_audio_sample_t *) jack_port_get_buffer (output_port[1], nframes);
  jack_default_audio_sample_t *in_l =   (jack_default_audio_sample_t *) jack_port_get_buffer (input_port[0], nframes);
  jack_default_audio_sample_t *in_r =   (jack_default_audio_sample_t *) jack_port_get_buffer (input_port[1], nframes);
  // These are now pointers to the buffer spaces which can be read/written to.
  // If we're not done starting up just memcpy in to out.
  if (state == CHANGELING_STATE_STARTING) {
    memcpy (out_l, in_l, sizeof (jack_default_audio_sample_t) * nframes);
    memcpy (out_r, in_r, sizeof (jack_default_audio_sample_t) * nframes);
    return 0;
  }
  // Let's do some simple maths -once-
  size_t framesize = sizeof(jack_default_audio_sample_t);
  size_t maxsize = (framesize * nframes);
  if (state == CHANGELING_STATE_OUT) {
    // Okay - if we're OUT, we're just copying input to output
    memcpy (out_l, in_l, sizeof (jack_default_audio_sample_t) * nframes);
    memcpy (out_r, in_r, sizeof (jack_default_audio_sample_t) * nframes);
  } else if (state == CHANGELING_STATE_IN) {
    // If we're IN, then we're playing out our buffer in realtime
    // Input audio
    if(jack_ringbuffer_write_space(buffer_l) >= maxsize && jack_ringbuffer_write_space(buffer_r) >= maxsize) {
      for (jack_nframes_t frameNum=0; frameNum<nframes; frameNum++) {
        size_t l_written = jack_ringbuffer_write(buffer_l,(char *) & in_l[frameNum], framesize);
        size_t r_written = jack_ringbuffer_write(buffer_r,(char *) & in_r[frameNum], framesize);
        if (l_written < framesize) {
          cout << "Ringbuffer overrun for left channel" << endl;
        }
        if (r_written < framesize) {
          cout << "Ringbuffer overrun for right channel" << endl;
        }
      }
    }
    // Output audio from buffer
    if(jack_ringbuffer_read_space(buffer_l) >= maxsize && jack_ringbuffer_read_space(buffer_r) >= maxsize) {
      for (jack_nframes_t frameNum=0; frameNum<nframes; frameNum++) {
        size_t l_read = jack_ringbuffer_read(buffer_l,(char *) & out_l[frameNum], framesize);
        size_t r_read = jack_ringbuffer_read(buffer_r,(char *) & out_r[frameNum], framesize);
        if (l_read < framesize) {
          cout << "Ringbuffer underrun for left channel" << endl;
        }
        if (r_read < framesize) {
          cout << "Ringbuffer underrun for right channel" << endl;
        }
      }
    }
    
  } else if (state == CHANGELING_STATE_ENTERING) {
    // If we're ENTERING we're playing our jingle and recording to our buffer.
    if(jack_ringbuffer_read_space(buffer_l) >= maxsize && jack_ringbuffer_read_space(buffer_r) >= maxsize) {
      jack_default_audio_sample_t *frame_buffer = (jack_default_audio_sample_t*) malloc(framesize*2);
      for (jack_nframes_t frameNum=0; frameNum<nframes; frameNum++) {
        sf_count_t frames_read = buffer_file.readf((float*)frame_buffer, 1);
        if (frames_read == 0) {
          // Rewind to the start of the file
          buffer_file.seek(0, SEEK_SET);
          // We're done buffering, cool! Onwards!
          cout << "Done buffering, transitioning to IN" << endl;
          state = CHANGELING_STATE_IN;
          break; // we don't need to go on any further in this loop
        } 
        // If we've got frames, copy them from the framebuffer into the output buffer at the right point
        memcpy(&out_l[frameNum], &frame_buffer[0], framesize);
        memcpy(&out_r[frameNum], &frame_buffer[1], framesize);
      }
      free(frame_buffer);
    }
    // Record to buffer
    if(jack_ringbuffer_write_space(buffer_l) >= maxsize && jack_ringbuffer_write_space(buffer_r) >= maxsize) {
      for (jack_nframes_t frameNum=0; frameNum<nframes; frameNum++) {
        size_t l_written = jack_ringbuffer_write(buffer_l,(char *) & in_l[frameNum], framesize);
        size_t r_written = jack_ringbuffer_write(buffer_r,(char *) & in_r[frameNum], framesize);
        if (l_written < framesize) {
          cout << "Ringbuffer overrun for left channel" << endl;
        }
        if (r_written < framesize) {
          cout << "Ringbuffer overrun for right channel" << endl;
        }
      }
    }

  } else if (state == CHANGELING_STATE_LEAVING) {
    // If we're LEAVING we're just writing out the buffer.
    // Output audio from buffer
    if(jack_ringbuffer_read_space(buffer_l) >= maxsize && jack_ringbuffer_read_space(buffer_r) >= maxsize) {
      for (jack_nframes_t frameNum=0; frameNum<nframes; frameNum++) {
        size_t l_read = jack_ringbuffer_read(buffer_l,(char *) & out_l[frameNum], framesize);
        size_t r_read = jack_ringbuffer_read(buffer_r,(char *) & out_r[frameNum], framesize);
        if (l_read < framesize) {
          cout << "Ringbuffer underrun for left channel" << endl;
        }
        if (r_read < framesize) {
          cout << "Ringbuffer underrun for right channel" << endl;
        }
      }
    }
    if (jack_ringbuffer_read_space(buffer_l) == 0 || jack_ringbuffer_read_space(buffer_r) == 0) {
      // We've run out of buffer! That means we're all done. Go OUT.
      state = CHANGELING_STATE_OUT;
    }
  } else if (state == CHANGELING_STATE_DUMPING) {
    // If we're DUMPING, we want to wipe our buffers and return to ENTERING, doing one frame of OUT's 1-1 copying.
    memcpy (out_l, in_l, sizeof (jack_default_audio_sample_t) * nframes);
    memcpy (out_r, in_r, sizeof (jack_default_audio_sample_t) * nframes);
    // Now wipe the buffer entirely
    jack_ringbuffer_reset(buffer_l);
    jack_ringbuffer_reset(buffer_r);
    // Don't forget to reset our seek position
    buffer_file.seek(0, SEEK_SET);
    state = CHANGELING_STATE_ENTERING;
  }
  return 0;      
}

void on_mqtt_message(void *obj, const mosquitto_message *msg) {
  if(msg->payloadlen){
    char * pl = new char [msg->payloadlen];
    strcpy (pl, (char*)msg->payload);
    //const char * pl = msg->payload;
    cout << "Received command on " << msg->topic << ": " << msg->payload << endl;
    if (strcmp(pl,"EXIT") == 0 && state == CHANGELING_STATE_IN) {
      cout << "Commanded to exit!" << endl;
      state = CHANGELING_STATE_LEAVING;
    } else if (strcmp(pl,"ENTER") == 0 && state == CHANGELING_STATE_OUT) {
      cout << "Commanded to enter!" << endl;
      state = CHANGELING_STATE_ENTERING;
    } else if (strcmp(pl,"DUMP") == 0 && state != CHANGELING_STATE_OUT && state != CHANGELING_STATE_DUMPING) {
      cout << "Commanded to dump buffers!" << endl;
      state = CHANGELING_STATE_DUMPING;
    }
  }
}
/**
The main program loop.
*/
int main(int argc, char *argv[]) {
  printf("Changeling Profanity Delay - Starting up\n");
  if (argc != 2) {
    fprintf(stderr, "Expecting wav file as argument\n");
    return 1;
  }
  signal(SIGINT,int_handler); // catch SIGINT
  // Connect to our MQTT broker
  mosquitto_lib_init();
  mqtt_client = mosquitto_new("changeling", NULL);
  int res = mosquitto_connect(mqtt_client, "localhost", 1883, 5, true);
  if (res != MOSQ_ERR_SUCCESS) {
    fprintf(stderr, "Couldn't connect to MQTT broker on localhost:1883.");
    return 1;
  }
  mosquitto_subscribe(mqtt_client, NULL, "changeling-commands", 1);
  mosquitto_message_callback_set(mqtt_client, on_mqtt_message);

  // Open our jingle
  buffer_file = SndfileHandle(argv[1], SFM_READ);
  if (!buffer_file) {
    fprintf(stderr, "Error reading buffer file '%s'\n", argv[1]);
    return 1;
  }
  buffer_file.seek(0, SEEK_SET);
  jack_set_error_function(error); // and JACK errors
  if ((client = jack_client_open("changeling", (jack_options_t)0, NULL)) == NULL) {
    fprintf(stderr, "Couldn't start - jack server not running?\n");
    return 1;
  }
  // register callbacks
  jack_on_shutdown(client, jack_shutdown, 0);
  jack_set_process_callback(client, process, 0);
  // setup ports
  input_port[1] = jack_port_register(client, "input_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
  input_port[0] = jack_port_register(client, "input_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
  output_port[1] = jack_port_register(client, "output_2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  output_port[0] = jack_port_register(client, "output_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  // activate client
  if (jack_activate(client)) {
    fprintf(stderr, "cannot activate client!");
    return 1;
  }
  // get sample rate and initialise internal buffers for given delay length
  sample_rate = jack_get_sample_rate(client);
  cout << "JACK Sample rate: "<< sample_rate << endl;

  if ((uint)buffer_file.samplerate() != sample_rate) {
    fprintf(stderr, "Buffer file should be of sample rate %d\n",sample_rate);
    return 1;
  }

  // Check channels - mono
  if (buffer_file.channels() != 2) {
    fprintf(stderr, "Wrong number of channels in buffer file, should be stereo\n");
    return 1;
  }


  // buffers
  max_delay_samples = (jack_nframes_t)buffer_file.frames();
  max_delay_seconds = (max_delay_samples/sample_rate);
  cout << "Setting up delay with " << max_delay_seconds << " seconds of delay, " << max_delay_samples << " samples" << endl;
  buffer_l = jack_ringbuffer_create(max_delay_samples*sizeof(jack_default_audio_sample_t));
  buffer_r = jack_ringbuffer_create(max_delay_samples*sizeof(jack_default_audio_sample_t));
  int res_l = jack_ringbuffer_mlock(buffer_l);
  int res_r = jack_ringbuffer_mlock(buffer_r);
  if ( res_l || res_r ) {
    cout << "Error locking memory for realtime operation! Ensure you have permissions to lock memory, this is often in your OS's /etc/security/limits.conf file." << endl;
    exit(-1);
  }
  // connect ports
  const char **ports;
  if ((ports = jack_get_ports(client, NULL, NULL, JackPortIsPhysical|JackPortIsOutput)) == NULL) {
    fprintf(stderr, "Cannot find any physical capture ports\n");
    //exit(1);
  }
  if (jack_connect(client, ports[0], jack_port_name (input_port[0]))) {
    fprintf (stderr, "Cannot connect input port (left)\n");
  }
  if (jack_connect(client, ports[1], jack_port_name (input_port[1]))) {
    fprintf (stderr, "Cannot connect input port (right)\n");
  }
  free (ports);
  if ((ports = jack_get_ports(client, NULL, NULL, JackPortIsPhysical|JackPortIsInput)) == NULL) {
    fprintf(stderr, "Cannot find any physical playback ports\n");
    //exit(1);
  }
  if (jack_connect (client, jack_port_name (output_port[0]), ports[0])) {
    fprintf (stderr, "cannot connect output port (left)\n");
  }
  if (jack_connect (client, jack_port_name (output_port[1]), ports[1])) {
    fprintf (stderr, "cannot connect output port (right)\n");
  }
  
  free (ports);
  // We now have a full set of connected ports.

  // We're ready to go!
  state = CHANGELING_STATE_ENTERING;
  // And now we want to loop endlessly while we're running.
  while(state != CHANGELING_STATE_EXITING) {
    if(state != last_state) {
      if (state == CHANGELING_STATE_ENTERING)
        cout << "Entering delay, playing jingle+recording..." << endl;
      if (state == CHANGELING_STATE_LEAVING)
        cout << "Leaving delay, get ready for a little jolt, fellas..." << endl;
      if (state == CHANGELING_STATE_IN)
        cout << "Now in delay state, "<< max_delay_seconds<<" seconds between input and output" << endl;
      if (state == CHANGELING_STATE_OUT)
        cout << "Delay out, no delay between input and output, cannot dump!" << endl;
      last_state = state;
    }
    // Now construct our message
    stringstream msg;
    time_t rawtime;
    struct tm * timeinfo;
    time ( &rawtime );
    timeinfo = localtime ( &rawtime );
    char buffer[14];
    strftime (buffer,14,"%H:%M:%S - ",timeinfo);
    msg << buffer;
    msg << "STATE=";
    if (state == CHANGELING_STATE_OUT) {
      msg << "OUT";
    } else if (state == CHANGELING_STATE_IN) {
      msg << "IN";
    } else if (state == CHANGELING_STATE_LEAVING) {
      msg << "LEAVING";
    } else if (state == CHANGELING_STATE_ENTERING) {
      msg << "ENTERING";
    } else if (state == CHANGELING_STATE_DUMPING) {
      msg << "DUMPING";
    } else if (state == CHANGELING_STATE_EXITING) {
      msg << "EXITING";
    }
    msg << ";";
    //msg << "BUFFER_BYTES=" << jack_ringbuffer_read_space(buffer_l) << ";";
    //msg << "BUFFER_SAMPLES=" << jack_ringbuffer_read_space(buffer_l)/sizeof(jack_default_audio_sample_t) << ";";
    msg << "BUFFER_SECONDS=" << (jack_ringbuffer_read_space(buffer_l)/sizeof(jack_default_audio_sample_t))/(float)sample_rate << ";";
    //char * cstr = new char [msg.str().size()+1];
    //strcpy (cstr, msg.str().c_str());
    const char * cstr = msg.str().c_str();
    // Send it
    mosquitto_publish(mqtt_client, NULL, "changeling-status", (sizeof(*cstr)*msg.str().size()), (uint8_t*)cstr, 1, false);
    // MQTT loop
    mosquitto_loop(mqtt_client, 100);
    cout << msg.str().c_str() << endl;
    usleep(100000); // MICROseconds. NOT milliseconds.
  }
  mosquitto_destroy(mqtt_client);
  mosquitto_lib_cleanup();
  jack_ringbuffer_free(buffer_l);
  jack_ringbuffer_free(buffer_r);
  jack_client_close (client);
  return 0;
}


