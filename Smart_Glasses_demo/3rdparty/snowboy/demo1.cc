// example/C++/demo.cc

// Copyright 2016  KITT.AI (author: Guoguo Chen)

#include <cassert>
#include <csignal>
#include <iostream>
#include "AudioProcess.h"
#include <portaudio.h>
#include <string>
#include <vector>

#include "include/snowboy-detect.h"


int main(int argc, char* argv[]) {
  std::string usage =
      "Example that shows how to use Snowboy in C++. Parameters are\n"
      "hard-coded in the parameter section. Please check the source code for\n"
      "more details. Audio is captured by PortAudio.\n"
      "\n"
      "To run the example:\n"
      "  ./demo\n";

  // Checks the command.
  if (argc > 1) {
    std::cerr << usage;
    exit(1);
  }


  std::string resource_filename = "resources/common.res";
  std::string model_filename = "resources/models/echo.pmdl";
  std::string sensitivity_str = "0.5";
  float audio_gain = 1;
  bool apply_frontend = false;

  // Initializes Snowboy detector.
  snowboy::SnowboyDetect detector(resource_filename, model_filename);
  detector.SetSensitivity(sensitivity_str);
  detector.SetAudioGain(audio_gain);
  detector.ApplyFrontend(apply_frontend);

  // Initializes PortAudio. You may use other tools to capture the audio.
  AudioProcess audio_process;
  audio_process.startRecording();

  // Runs the detection.
  // Note: I hard-coded <int16_t> as data type because detector.BitsPerSample()
  //       returns 16.
  std::cout << "Listening... Press Ctrl+C to exit" << std::endl;
  std::vector<int16_t> data;
  while (true) {
    audio_process.getRecordedAudio(data);
    if (data.size() != 0) {
      int result = detector.RunDetection(data.data(), data.size());
      if (result > 0) {
        std::cout << "Hotword " << result << " detected!" << std::endl;
      }
    }
  }

  return 0;
}