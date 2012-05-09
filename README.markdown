# Changeling - a profanity delay

Changeling is a simple profanity delay intended for live broadcasting of events where control over language used by performers is not guaranteed or possible. It's lightweight, written in C++, uses the JACK audio connection kit for audio I/O, and is network-controllable.

Rather than do 'clever' things like stretching audio or extending silence periods to build delay transparently it is assumed that multiple rapid activations may be required and thus builds delay by playing a sound file. The length of this sound file defines the length of delay. 

This means when the delay is inline at any point, audio to air is buffered, and dumping the buffer just results in the sound file being played again to fill a new buffer, meaning that unbuffered audio can never go to air (as with 'build delay via stretching material' delays). The downside is that this potentially sounds quite odd. But it's better than losing your license because you just broadcast a string of obscenities.

## Usage

Changeling is intended for use on a single computer doing nothing but changeling and attempts to attach itself to the first two physical input and output ports. If none are found or you need a more complex setup, it will require manual connection. Client name and port names don't change so this is pretty straightforward to do.

Control of the delay is done via a MQTT broker. This lets you plug in various control mechanisms for the delay really simply, as well as getting feedback from the delay itself in a multi-client compatible manner.

You run the delay as ```changeling path/to/a/stereo.wav```. The WAV file provided defines the length of the delay and must be the same sample rate as the JACK server used. 48kHz is strongly recommended for broadcast applications.

### Operational States and Transitions

The delay starts up OUT - that is, it passes audio transparently without any modification.

Once told to enter the delay, the ENTERING state records into a buffer while playing out the contents of the WAV file. Once the file has finished playing the delay automatically transitions to the IN state. At this point there is a delay of n seconds between audio entering the delay and leaving the delay, where n is the length of the file. The IN state is our normal operating state.

We can DUMP the buffer at _any time_ (except when OUT), at which point we go to the ENTERING state again immediately. This discards whatever was previously in the buffer - hopefully including your overenthusiastic guest's colourful language.

Once we're done with our delayed program material we enter the LEAVING state. This simply stops filling the buffer and plays the remaining buffer until there is no more to play, and then flips back to the OUT state. The assumption is that your audio source is muted until after entering the LEAVING state and then you can start making noise again once you're OUT.

### MQTT API

The MQTT protocol permits both control and monitoring of the delay. The MQTT protocol is very lightweight and has been implemented for low-cost devices like the Nanode, Arduino+Ethernet, Netduino and mbed microcontroller development platforms. Additionally there's great support in loads of programming languages, so you can write web applications, GTK/Qt/WX GUIs, or command line apps with relative ease.

The delay broadcasts a current-state message every 200ms to a broker running on localhost, which clients can pick up and display to an end user. This broadcast includes information on the current state of the delay as well as buffer information.

The delay also accepts control over MQTT, allowing you to:

* Enter the delay (at any point when OUT)
* Dump the buffer (at any point except when OUT)
* Leave the delay (at any point when IN)

These are done by sending one of the respective strings to the ```changeling-commands``` topic:

* ENTER
* DUMP
* EXIT

Feedback from the delay and updates are broadcast on the ```changeling-status``` topic with a message format of:

    HH:MM:SS - STATE=(IN|OUT|ENTERING|LEAVING|DUMPING);BUFFER_SECONDS=float;

This is: Timestamp, state, buffer size in seconds (should be 0 when OUT and the length of the jingle file when IN). Parse to your heart's content; more data may be added in a future version in the same form of KEY=VAL;KEY=VAL;, so keep that in mind when writing clients.

#### mosquitto\_pub and mosquitto\_sub 

You can use mosquitto\_pub and mosquitto\_sub for simple control of the delay with no external dependencies or code.

    # To enter delay
    mosquitto_pub -t changeling-commands -m ENTER
    # To exit delay
    mosquitto_pub -t changeling-commands -m EXIT
    # To dump delay buffers
    mosquitto_pub -t changeling-commands -m EXIT

And for listening to the result of these commands:

    # To listen to status messages
    mosquitto_sub -t changeling-status


## Dependencies and Compilation

To compile and use changeling you need the following:

* A C++ compilation environment (gcc and friends)
* CMake 2.6 or greater
* The JACK Audio Connection Kit development headers (libjack)
* The libsndfile development headers
* The libmosquitto development headers

On Ubuntu, installing these can be done with:

    sudo apt-get install libjack-jackd2-dev libsndfile1-dev libmosquitto0-dev cmake build-essential

Compilation is the usual cmake pattern of:
    
    cd changeling
    mkdir build
    cd build
    cmake ../
    make
    sudo make install

To use changeling once compiled (or installed from package) you will need:

* A running JACK server
* A file to play to build up buffer (WAV, _same sample rate as your JACK server_, 48kHz recommended as ever)
* A sound card with at least one stereo input and one stereo output
* A MQTT broker on the same machine as changeling (such as mosquitto)

To install mosquitto and JACK on Ubuntu (plus mosquitto clients):

    sudo apt-get install jackd2 mosquitto mosquitto-clients

You may want to work with the delay from Python; python-mosquitto is recommended, can be installed on Ubuntu easily, has no dependencies above libmosquitto and an example read-only client is provided in the examples folder.


## Licensing and Credits

Changeling was developed by James Harrison (http://talkunafraid.co.uk/) for Insanity Radio 103.2 FM (http://www.insanityradio.com/).

Copyright (c) 2012, James Harrison.

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following  conditions are met:
    * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
    * Neither the name of the Changeling project nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL JAMES HARRISON OR OTHER CHANGELING CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
