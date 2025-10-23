#include <windows.h>
#include <mmeapi.h>
#include <iostream>
#include <conio.h>
#include <string>
#include <map>
#include <tuple>
#include <stdlib.h>
#include <stdio.h>
#include <mutex>
#include <cassert>

#pragma comment(lib, "winmm.lib")

class MidiButtonStates {
    public:

        int deviceID;
        
        struct ButtonState {
            bool isActive = false;
            int inactiveColor = 0x01;
            int activeColor = 0x7F;
            int buttonNumber = 0;
        };

        std::map<int, ButtonState> buttonStates;

        std::mutex stateMutex;
};

int sendMessage(HMIDIOUT device, MIDIHDR *buffer) {
    MMRESULT prepHeaderResult = midiOutPrepareHeader(device, buffer, sizeof(*buffer));
    if (prepHeaderResult != MMSYSERR_NOERROR) {
        std::cerr << "Error preparing header: " << prepHeaderResult << std::endl;
        return 1;
    }
    MMRESULT longMsgResult = midiOutLongMsg(device, buffer, sizeof(*buffer));
    if (longMsgResult != MMSYSERR_NOERROR) {
        std::cerr << "Error sending message: " << longMsgResult << std::endl;
        return 1;
    }
    MMRESULT unprepHeaderResult = midiOutUnprepareHeader(device, buffer, sizeof(*buffer));
    if (unprepHeaderResult != MMSYSERR_NOERROR) {
        std::cerr << "Error unpreparing header: " << unprepHeaderResult << std::endl;
        return 1;
    }
    return 0;
}

/*
    00 - black
    01 - red
    04 - green
    05 - yellow
    10 - blue
    11 - magenta
    14 - cyan
    7F - white
*/
int lightButton(int deviceID, MidiButtonStates::ButtonState *buttonState) {
    assert(buttonState->buttonNumber <= 0xF);

    HMIDIOUT arturia;
    UINT arturiaID = (UINT) deviceID;
    MMRESULT openOutResult = midiOutOpen(&arturia, arturiaID, NULL, 0, CALLBACK_NULL);

    if (openOutResult != MMSYSERR_NOERROR) {
        std::cerr << "Failed to open MIDI output device: " << arturiaID << std::endl;
        return 1;
    } else {
        // std::cout << "Opened MIDI output device" << std::endl;
    }
    
    char *sysexData = (char*) malloc(12);

    if (sysexData == NULL) {
        std::cerr << "Memory allocation failed." << std::endl;
        return 1;
    }

    char colorCode;
    char value;
    if (buttonState->isActive) {
        colorCode = buttonState->activeColor;
        value = 0x7F;
    } else {
        colorCode = buttonState->inactiveColor;
        value = 0x00;
    }

    sysexData[0] = 0xF0;
    sysexData[1] = 0x00;
    sysexData[2] = 0x20;
    sysexData[3] = 0x6B;
    sysexData[4] = 0x7F;
    sysexData[5] = 0x42;
    sysexData[6] = 0x02;
    sysexData[7] = 0x00;
    sysexData[8] = 0x10;
    sysexData[9] = 0x70 + buttonState->buttonNumber; // Button Select
    sysexData[10] = colorCode; // Color Code
    sysexData[11] = 0xF7;
            
    MIDIHDR buffer;
    buffer.lpData = sysexData;
    buffer.dwBufferLength = 16;
    buffer.dwBytesRecorded = 12;
    buffer.dwFlags = 0;
    
    int result = sendMessage(arturia, &buffer);
    if (result != 0) {
        free(sysexData);
        return 1;
    }
    
    MMRESULT closeOutResult = midiOutClose(arturia);

    if (closeOutResult != MMSYSERR_NOERROR) {
        std::cerr << "Failed to close MIDI output device: " << arturiaID << std::endl;
        free(sysexData);
        return 1;
    } else {
        // std::cout << "Closed MIDI output device" << std::endl;
    }

    free(sysexData);
    return 0;
}

void CALLBACK MidiInProc(
   HMIDIIN   hMidiIn,
   UINT      wMsg,
   DWORD_PTR dwInstance,
   DWORD_PTR dwParam1,
   DWORD_PTR dwParam2
) {
   if (wMsg == MIM_DATA) {
        DWORD msgType = 0xFF & dwParam1; // 7F is mute; 0 is unmute
        DWORD hwID = 0xFF & (dwParam1 >> 8);
        DWORD toggleState = 0xFF & (dwParam1 >> 16);
        MidiButtonStates *states = (MidiButtonStates*) dwInstance;

        std::cout << "hwid: " << std::hex << hwID << " toggleState: " << std::hex << toggleState << " msgType: " << msgType << std::endl;

        if (msgType == 0xb0) { 
            if (toggleState == 0x7F) {
            states->buttonStates[hwID].isActive = false;
            } else {
                states->buttonStates[hwID].isActive = true;
            }
        } else if (msgType == 0x89) {//89 is keyup
            std::lock_guard<std::mutex> lock(states->stateMutex);
            hwID = hwID + 80; // I have midiMixer configured to send values from 100 - 107 for buttons 0 - 7 but on a hardware level, these buttons are configured to send values 20-27
            int errCode = lightButton(states->deviceID, &states->buttonStates[hwID]);
        }
    }
} 

std::string getFirstWord(std::string deviceName) {
    int delimIdx = deviceName.find(" ");
    std::string token = deviceName.substr(0, delimIdx);
    return token;
}

std::map<std::string, int> getInDevices() {
    UINT numDevices = 0;
    numDevices = midiInGetNumDevs();

    std::map<std::string, int> inDevices; 
    
    MIDIINCAPSA inCaps;
    for (UINT i = 0; i < numDevices; i++) {
        MMRESULT cur = midiInGetDevCapsA(i, &inCaps, sizeof(MIDIINCAPSA));
        std::cout << "input device " << i << " name: " << inCaps.szPname << std::endl;
        inDevices[getFirstWord(inCaps.szPname)] = i;
    }

    return inDevices;
}

std::map<std::string, int> getOutDevices() {
    UINT numDevices = 0;
    numDevices = midiOutGetNumDevs();
    std::map<std::string, int> outDevices;

    MIDIOUTCAPSA outCaps;
    for(UINT i=0; i < numDevices; i++) {
        MMRESULT cur = midiOutGetDevCapsA(i, &outCaps, sizeof(MIDIINCAPSA));
        std::cout << "output device " << i << " name: " << outCaps.szPname << std::endl;
        outDevices[getFirstWord(outCaps.szPname)] = i;
    }

    return outDevices;
}

int main() {
    std::string outputDeviceCode = "Arturia";
    std::string inputDeviceCode = "loopMIDI-cpp";

   
    std::map<std::string, int> outDevices = getOutDevices();
    // for (const auto& pair : outDevices) {
    //     std::cout << pair.first << " : " << pair.second << std::endl;
    // }
    std::map<std::string, int> inDevices = getInDevices();

    UINT outputDeviceID;
    try {  // ? this try catch is not working, I think. So even if it fails to open it just puts some random number in the id
        outputDeviceID = (UINT) outDevices.at(outputDeviceCode);
        std::cout << "Found output device " << outputDeviceCode << " at ID " << outputDeviceID << std::endl;
    } catch (const std::out_of_range &e) {
        std::cerr << "Output device " << outputDeviceCode << " not detected at port " << outputDeviceID << std::endl;
        return 1;
    }

    UINT inputDeviceID;
    try {
        inputDeviceID = (UINT) inDevices.at(inputDeviceCode);
        std::cout << "Found input device " << inputDeviceCode << " at ID " << inputDeviceID << std::endl;
    } catch (const std::out_of_range &e) {
        std::cerr << "Input device " << inputDeviceCode << " not detected at port " << inputDeviceID << std::endl;
        return 1;
    }

    /*
    00 - black
    01 - red
    04 - green
    05 - yellow
    10 - blue
    11 - magenta
    14 - cyan
    7F - white
    */

    MidiButtonStates *states = new MidiButtonStates();
    states->buttonStates[0x64].buttonNumber = 0;
    states->buttonStates[0x65].buttonNumber = 1;
    states->buttonStates[0x66].buttonNumber = 2;
    states->buttonStates[0x67].buttonNumber = 3;
    states->buttonStates[0x68].buttonNumber = 4;
    states->buttonStates[0x69].buttonNumber = 5;
    states->buttonStates[0x6a].buttonNumber = 6;
    states->buttonStates[0x6b].buttonNumber = 7;


    states->buttonStates[0x64].activeColor = 0x7F; // Motu
    states->buttonStates[0x65].activeColor = 0x04; // Spotify
    states->buttonStates[0x66].activeColor = 0x10; // Discord
    states->buttonStates[0x67].activeColor = 0x05; // Firefox
    states->buttonStates[0x68].activeColor = 0x11;
    states->buttonStates[0x69].activeColor = 0x11;
    states->buttonStates[0x6a].activeColor = 0x11;
    states->buttonStates[0x6b].activeColor = 0x00; // Assign

    states->buttonStates[0x6b].inactiveColor = 0x14;

    states->deviceID = outputDeviceID;
    
    HMIDIIN loopMIDI;
    MMRESULT openInResult = midiInOpen(&loopMIDI, inputDeviceID, (DWORD_PTR)MidiInProc, (DWORD_PTR)states, CALLBACK_FUNCTION);
    if (openInResult != MMSYSERR_NOERROR) {
        char *errorBuffer = (char *) malloc(sizeof(char) * 1000);
        midiInGetErrorTextA(openInResult, errorBuffer, 1000);
        std::cerr << "Error opening input MIDI device. MMRESULT: " << errorBuffer << std::endl;
        free(errorBuffer);
        delete(states);
        return 1;
    }

    midiInStart(loopMIDI);

    while (true) {
        if (_kbhit()) {
            int c = _getch();
            if (c == 'q' || c == 'Q') {
                break;
            }
        }
        Sleep(100); 
    }

    midiInStop(loopMIDI);
    std::cout << "Stopping MIDI input." << std::endl;
    
    MMRESULT closeInResult = midiInClose(loopMIDI);
    if (closeInResult != MMSYSERR_NOERROR) {
        std::cerr << "Error closing MIDI device. MMRESULT: " << closeInResult << std::endl;
        delete(states);
        return 1;
    }

    delete(states);
    return 0;
}