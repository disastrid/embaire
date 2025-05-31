#include <Bela.h>
#include <libraries/Scope/Scope.h>
#include <libraries/Biquad/Biquad.h>
#include <libraries/Midi/Midi.h>
#include <libraries/AudioFile/AudioFile.h>
#include <libraries/OscSender/OscSender.h>
#include <vector>
#include <string>
#include <algorithm>

#define NUM_KEYS 8 // Number of keys in effect

// filters
std::vector<Biquad> lp(NUM_KEYS);
// Scope
Scope scope;
// Midi 
Midi midi;
const char* gMidiPort0 = "hw:0,0,0";

// OSC
OscSender oscSender;
int remotePort = 7563;
const char* remoteIp = "192.168.7.1";



float gLPfreq = 200.0;	// Cut-off frequency for low-pass filter (Hz)
float gFilterQ = 0.707; // Quality factor for the biquad filters to provide a Butterworth response

// Write outputs:
std::vector<std::vector<float>> gOutputs;
std::string gFilenameOutputs = "outputs.wav";
unsigned int gWrittenFrames = 0; // how many frame have actually been written to gOutputs
unsigned int gDurationSec = 20;

float gPitchBend;
float gLargestSample = 0;

int gNoteState = 0;
int gPitchBendState = 0;

float gKeyValues[NUM_KEYS] = { 0.0 };

// Variables for counting samples

int gPiezoPeakTime = 1000;
int gPiezoDebounceTime = 2500;

int gLookingForPiezoPeakCounter[NUM_KEYS] = { 0 }; // Counters for each piezo

int gPiezoState[NUM_KEYS] = { 0 };
float gFinalPiezoAmplitude[NUM_KEYS] = { 0.0 }; // Stores highest value found over time
int gLookingForPiezoPeak_counter[NUM_KEYS] = { 0 }; // Counts the 5ms needed to find the peak.
int gPiezoDebounce_counter[3] = { 0 }; // Counts the time that we stop the piezos so we don't re-trigger.
float gHighestPiezoValueFound[3] = { 0 }; // The highest value we found over time.
int gPiezoVels[NUM_KEYS] = { 0 };

int gStrikeCount[NUM_KEYS] = { 0 };


float thresh = 0.2;
float gPeakDetector[NUM_KEYS] = { thresh, thresh, thresh };
float gRolloff = 0.001;

int gAudioFramesPerAnalogFrame = 2;



// HELPER FUNCTIONS

float readPiezo(BelaContext *context, unsigned int, unsigned int);
void sendMidi(int, float);

// PRESSURE SENSORS
float gPressure[NUM_KEYS] = { 0.0 };

bool setup(BelaContext *context, void *userData)
{
	// Set up scope:
	scope.setup(NUM_KEYS + 1, context->audioSampleRate);
	
	// Set up midi messaging:
	midi.writeTo(gMidiPort0); // We're writing to hw:0,0,0 .. in the original example we were receiving from hw:1,0,0
	
	// OSC
	oscSender.setup(remotePort, remoteIp);


	// Set up writing outputs:
	// unsigned int numFrames = context->audioSampleRate * gDurationSec;

	// gOutputs.resize(context->audioOutChannels);
	// // If we have too many channels or too many frames to store, we may run out of RAM and
	// // the program will fail to start.

	// for(unsigned int k = 0; k < gOutputs.size(); k++) {
	// 	gOutputs[k].resize(numFrames); // However long you want the recording to be, in samples
	// }
	// Set up lp filter:
	BiquadCoeff::Settings s = {
		.fs = context->audioSampleRate,
		.q = gFilterQ,
		.peakGainDb = 0,
	};
	
	// Setting cutoff and type for all the filters
	for(unsigned int b = 0; b < lp.size(); ++b)
	{
			s.type = BiquadCoeff::lowpass;
			s.cutoff = gLPfreq;
			lp[b].setup(s);
	}
	rt_printf("Num channels: %d\n", NUM_KEYS);
	rt_printf("Let's fucking gooooo\n");
	return true;
}

void render(BelaContext *context, void *userData)
{

	for(unsigned int n = 0; n < context->audioFrames; n++) {
		
		
		for (unsigned int k = 0; k < NUM_KEYS; k++) {
			
			
			// ** PRESSURE SENSORS
			// if(gAudioFramesPerAnalogFrame && !(n % gAudioFramesPerAnalogFrame)) {
			// // read analog inputs and update frequency and amplitude
			// 	gPressure[k] = analogRead(context, n/gAudioFramesPerAnalogFrame, k);
			//  if (gPressure[k] > 0.4) {
			// 		sendOsc(gPressure[k]); // send osc message
			// 	}
			// }// END ANALOG READS
			
			// Piezo states:
			
			// 0 - wait for strike
			// 1 - strike detected, look for peak
			// 2 - velocity found, send midi message
			// 3 - debounce timeout
			float thisSample = readPiezo(context, n, k);
			gKeyValues[k] = thisSample;
			
			// **** UNCOMMENT THIS TO WRITE AUDIO: ****
			
			// gOutputs[k][gWrittenFrames] = thisSample;
			// ++gWrittenFrames;
			// if(gWrittenFrames >= gOutputs[k].size()) {
			// // if we have processed enough samples an we have filled the pre-allocated buffer,
			// // stop the program
			// 	rt_printf("TIME'S UP");
			// 	Bela_requestStop();
			// 	return;
			// }
			if (gPiezoState[k] == 0) {
				// Process signal, wait for strike
				if (thisSample > thresh) {
					// gPeakDetector[k] = thisSample;
					gKeyValues[k] = thisSample;	
					rt_printf("Strike detected on key %d with velocity %d! Strike number %d!\n", k, gPiezoVels[k], gStrikeCount[k]++);
					// gHighestPiezoValueFound[k] = 0.0; // Reset the highest value
					gPiezoState[k] = 1;
				}
		
			} else if (gPiezoState[k] == 1) {
				// Strike detected, look for peak over gPiezoPeakTime samples
				gLookingForPiezoPeak_counter[k]++;
				if (gLookingForPiezoPeak_counter[k] < gPiezoPeakTime) {
					if (gHighestPiezoValueFound[k] <= thisSample) {
						gHighestPiezoValueFound[k] = thisSample;	
					}
				} else {
					rt_printf("Highest found: %f\n", gHighestPiezoValueFound[k]);
					gLookingForPiezoPeak_counter[k] = 0; // reset counter
					// gPeakDetector[k] = gHighestPiezoValueFound[k];
					gPiezoState[k] = 2;
				}
			} else if (gPiezoState[k] == 2) {
				sendMidi(k, gHighestPiezoValueFound[k]);
				gPiezoState[k] = 3;
			} else if (gPiezoState[k] == 3) {
				gHighestPiezoValueFound[k] = 0.0;
				gPiezoDebounce_counter[k]++;
				if (gPiezoDebounce_counter[k] > gPiezoDebounceTime) {
					gPiezoDebounce_counter[k] = 0;
					gPiezoState[k] = 0;
				}
			} // END STATE IF STATEMENT
		} // END CHANNEL FOR LOOP
	} // END FOR LOOP
	
}

void cleanup(BelaContext *context, void *userData)
{
	for(auto& o : gOutputs)
		o.resize(gWrittenFrames);
	AudioFileUtilities::write(gFilenameOutputs, gOutputs, context->audioSampleRate);
}

float readPiezo(BelaContext *context, unsigned int frame, unsigned int keyNum) {
	
    float sample = audioRead(context, frame, keyNum); // frame = this sample, keyNum = channel number

	float processedSample = lp[keyNum].process(sample);
	
    // Rectify:
    
    if (processedSample < 0.0) {
    	processedSample *= -1.0;
    }
    
    // Half wave rectify: 
    // if (processedSample < 0.0) {
    // 	processedSample = 0.0;
    // }
    
    return processedSample;	
}

void sendMidi(int channel, float velIn) {
	if (channel == 4) {
		velIn *= 0.9; // linear scaling of channel 5 because it's fucking wild
		if (velIn < 0) {
			velIn *= -1.0; // if that makes it less than 0 take the abs
		}
	}
	float mappedVel = map(velIn, thresh, 1.0, 20, 127);
	// rt_printf("Vel out: %f\n", mappedVel);
	gPiezoVels[channel] = (int)mappedVel; 
	midi_byte_t noteVal = 0;
	if (channel == 0) {
		noteVal = 0x00;
	} else if (channel == 1) {
		noteVal = 0x01;
	} else if (channel == 2) {
		noteVal = 0x02;
	} else if (channel == 3) {
		noteVal = 0x03;
	} else if (channel == 4) {
		noteVal = 0x04;
	} else if (channel == 5) {
		noteVal = 0x05;
	} else if (channel == 6) {
		noteVal = 0x06;
	} else if (channel == 7) {
		noteVal = 0x07;
	} 
	
	midi_byte_t statusByte = 0x9A; // Note on, channel 10 - 10011010 is 9A
	midi_byte_t note = noteVal;
	midi_byte_t velocity = mappedVel; // value : 0 to 127 that's already been mapped
	midi_byte_t bytes[3] = {statusByte, note, velocity};
	midi.writeOutput(bytes, 3); // Send the message
}

void sendOsc(float pressure) {
	// oscSender.newMessage("/pressure").add(pressure).send();
}

