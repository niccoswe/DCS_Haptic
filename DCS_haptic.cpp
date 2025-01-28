#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include "portaudio.h"  // Changed to use quotes
#include <boost/asio.hpp>  // Changed to use angle brackets
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include "sndfile.h"  // Changed to use quotes

// Configuration parameters
float AOA_Warning_Start, AOA_Warning_End, Stall_warning;
float AOA_warning_start_volume, AOA_warning_end_volume, Stall_warning_volume;
std::string AOA_warning_audio_file, Stall_warning_audio_file;
int AOA_warning_device_index, Stall_warning_device_index;
int AOA_warning_balance, Stall_warning_balance;

// Queue for sound playback
std::queue<std::tuple<std::string, float, int, int>> soundQueue; // Use device index instead of device name
std::mutex queueMutex;
std::condition_variable queueCondition;
bool stopCurrentSound = false;
bool soundPlaying = false;

// Buffers for preprocessed audio data
std::vector<float> AOA_warning_buffer;
std::vector<float> Stall_warning_buffer;
int AOA_warning_sampleRate, AOA_warning_channels;
int Stall_warning_sampleRate, Stall_warning_channels;

// Forward declaration of calculateVolume function
float calculateVolume(float AoA, float start, float end, float start_volume, float end_volume);

// Function to read configuration file
void readConfig() {
    std::ifstream config("configuration.cfg");
    if (!config.is_open()) {
        std::cerr << "Failed to open configuration file." << std::endl;
        exit(1);
    }
    std::string line;
    while (std::getline(config, line)) {
        // Skip empty lines
        if (line.empty()) continue;
        
        // Remove comments (everything after //)
        size_t commentPos = line.find("//");
        if (commentPos != std::string::npos) {
            line = line.substr(0, commentPos);
        }
        
        // Skip if line is empty after removing comments
        if (line.empty()) continue;

        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        
        std::istringstream iss(line);
        std::string key;
        if (std::getline(iss, key, '=')) {
            std::string value;
            if (std::getline(iss, value)) {
                // Trim whitespace from key and value
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);
                
                if (key == "AOA_Warning_Start") AOA_Warning_Start = std::stof(value);
                else if (key == "AOA_Warning_End") AOA_Warning_End = std::stof(value);
                else if (key == "Stall_warning") Stall_warning = std::stof(value);
                else if (key == "AOA_warning_start_volume") AOA_warning_start_volume = std::stof(value);
                else if (key == "AOA_warning_end_volume") AOA_warning_end_volume = std::stof(value);
                else if (key == "Stall_warning_volume") Stall_warning_volume = std::stof(value);
                else if (key == "AOA_warning_audio_file") AOA_warning_audio_file = value;
                else if (key == "Stall_warning_audio_file") Stall_warning_audio_file = value;
                else if (key == "AOA_warning_device_index") AOA_warning_device_index = std::stoi(value);
                else if (key == "AOA_warning_balance") AOA_warning_balance = std::stoi(value);
                else if (key == "Stall_warning_device_index") Stall_warning_device_index = std::stoi(value);
                else if (key == "Stall_warning_balance") Stall_warning_balance = std::stoi(value);
            }
        }
    }
    config.close();
    std::cout << "Configuration loaded successfully." << std::endl;
    std::cout << "AOA_Warning_Start: " << AOA_Warning_Start << std::endl;
    std::cout << "AOA_Warning_End: " << AOA_Warning_End << std::endl;
    std::cout << "Stall_warning: " << Stall_warning << std::endl;
    std::cout << "AOA_warning_start_volume: " << AOA_warning_start_volume << std::endl;
    std::cout << "AOA_warning_end_volume: " << AOA_warning_end_volume << std::endl;
    std::cout << "Stall_warning_volume: " << Stall_warning_volume << std::endl;
    std::cout << "AOA_warning_audio_file: " << AOA_warning_audio_file << std::endl;
    std::cout << "Stall_warning_audio_file: " << Stall_warning_audio_file << std::endl;
    std::cout << "AOA_warning_device_index: " << AOA_warning_device_index << std::endl;
    std::cout << "AOA_warning_balance: " << AOA_warning_balance << std::endl;
    std::cout << "Stall_warning_device_index: " << Stall_warning_device_index << std::endl;
    std::cout << "Stall_warning_balance: " << Stall_warning_balance << std::endl;
}

// Function to list all available audio devices and their indices
void listAudioDevices() {
    Pa_Initialize();
    int numDevices = Pa_GetDeviceCount();
    const PaDeviceInfo* deviceInfo;
    const PaHostApiInfo* hostApiInfo;
    
    std::cout << "\nAvailable output devices:" << std::endl;
    std::cout << "------------------------" << std::endl;
    
    // First list WASAPI devices
    std::cout << "WASAPI devices (recommended):" << std::endl;
    for (int i = 0; i < numDevices; ++i) {
        deviceInfo = Pa_GetDeviceInfo(i);
        if (deviceInfo->maxOutputChannels > 0) {
            hostApiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
            if (std::string(hostApiInfo->name).find("WASAPI") != std::string::npos) {
                std::cout << "  [" << i << "] " << deviceInfo->name 
                         << " - Channels: " << deviceInfo->maxOutputChannels
                         << std::endl;
            }
        }
    }
    
    // Then list other devices
    std::cout << "\nOther audio interfaces:" << std::endl;
    for (int i = 0; i < numDevices; ++i) {
        deviceInfo = Pa_GetDeviceInfo(i);
        if (deviceInfo->maxOutputChannels > 0) {
            hostApiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
            if (std::string(hostApiInfo->name).find("WASAPI") == std::string::npos) {
                std::cout << "  [" << i << "] " << deviceInfo->name 
                         << " (" << hostApiInfo->name << ")"
                         << " - Channels: " << deviceInfo->maxOutputChannels
                         << std::endl;
            }
        }
    }
    std::cout << "\nNote: WASAPI is recommended for Windows systems.\n" << std::endl;
    
    Pa_Terminate();
}

// Function to load audio data into buffer
bool loadAudioData(const std::string& filename, std::vector<float>& buffer, int& sampleRate, int& channels) {
    SF_INFO sfInfo;
    SNDFILE* sndFile = sf_open(filename.c_str(), SFM_READ, &sfInfo);
    if (!sndFile) {
        std::cerr << "Failed to open audio file: " << filename << std::endl;
        return false;
    }

    buffer.resize(sfInfo.frames * sfInfo.channels);
    sf_read_float(sndFile, buffer.data(), buffer.size());
    sf_close(sndFile);

    sampleRate = sfInfo.samplerate;
    channels = sfInfo.channels;
    return true;
}

// Function to adjust volume based on AoA
float calculateVolume(float AoA, float start, float end, float start_volume, float end_volume) {
    if (AoA <= start) return start_volume;
    if (AoA >= end) return end_volume;
    return start_volume + (end_volume - start_volume) * (AoA - start) / (end - start);
}

// Function to apply a limiter to the audio signal
void applyLimiter(std::vector<float>& buffer, float threshold) {
    for (auto& sample : buffer) {
        if (sample > threshold) {
            sample = threshold;
        } else if (sample < -threshold) {
            sample = -threshold;
        }
    }
}

// Function to preprocess audio data
void preprocessAudioData(const std::string& file, float volume, int balance, std::vector<float>& buffer, int& sampleRate, int& channels) {
    std::string filePath = "audio/" + file; // Correct the path construction
    if (!loadAudioData(filePath, buffer, sampleRate, channels)) {
        std::cerr << "Failed to load audio data: " << filePath << std::endl;
        return;
    }

    float leftVolume = volume * (1.0f - balance / 100.0f);
    float rightVolume = volume * (1.0f + balance / 100.0f);
    for (size_t i = 0; i < buffer.size(); i += channels) {
        buffer[i] *= leftVolume / 100.0f;
        if (channels > 1) {
            buffer[i + 1] *= rightVolume / 100.0f;
        }
    }

    // Apply limiter to the audio signal
    applyLimiter(buffer, 0.9f); // Set threshold to 0.9 to prevent clipping
}

// Add global variables for volume scaling
float aoa_warning_scaling = 1.0f;
float stall_warning_scaling = 1.0f;

// Function to analyze audio peak levels and calculate safe volume scaling
float analyzeAudioLevels(const std::vector<float>& buffer, const std::string& warningType) {
    float maxPeak = 0.0f;
    for (const float& sample : buffer) {
        float absSample = std::abs(sample);
        if (absSample > maxPeak) {
            maxPeak = absSample;
        }
    }
    
    // Calculate scaling factor needed to prevent clipping at max volume
    float maxDesiredPeak = 0.7f; // Leave some headroom
    float safeScaling = (maxPeak > 0.0f) ? maxDesiredPeak / maxPeak : 1.0f;
    
    std::cout << warningType << " audio analysis - Max peak: " << maxPeak 
              << ", Safe scaling factor: " << safeScaling << std::endl;
    
    return safeScaling;
}

// Function to find the correct WASAPI device index
int findWasapiDevice(int requestedIndex) {
    int numDevices = Pa_GetDeviceCount();
    int wasapiCount = 0;
    
    for (int i = 0; i < numDevices; ++i) {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
        const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
        
        if (deviceInfo->maxOutputChannels > 0 && 
            std::string(hostApiInfo->name).find("WASAPI") != std::string::npos) {
            if (wasapiCount == requestedIndex) {
                return i;
            }
            wasapiCount++;
        }
    }
    return requestedIndex; // Fallback to original index if not found
}

// Function to play preprocessed sound with continuous playback
void playPreprocessedSound(const std::vector<float>& buffer, int sampleRate, int channels, int deviceIndex, float volume, int balance) {
    if (buffer.empty()) {
        std::cerr << "Error: Audio buffer is empty" << std::endl;
        return;
    }

    static PaStream* stream = nullptr;
    static bool streamInitialized = false;

    if (!streamInitialized) {
        Pa_Initialize();
        
        // Get the actual device info
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(deviceIndex);
        if (!deviceInfo) {
            std::cerr << "Error: Could not get device info for index " << deviceIndex << std::endl;
            return;
        }

        const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
        std::cout << "Using audio device: " << deviceInfo->name 
                  << " with API: " << hostApiInfo->name 
                  << "\nDevice sample rate: " << deviceInfo->defaultSampleRate
                  << "\nRequested sample rate: " << sampleRate << std::endl;

        // Use device's default sample rate instead of the audio file's rate
        PaStreamParameters outputParameters;
        outputParameters.device = deviceIndex;
        outputParameters.channelCount = channels;
        outputParameters.sampleFormat = paFloat32;
        outputParameters.suggestedLatency = deviceInfo->defaultLowOutputLatency;
        outputParameters.hostApiSpecificStreamInfo = nullptr;

        // Try to open stream with error checking
        PaError err = Pa_OpenStream(&stream,
                                  nullptr,
                                  &outputParameters,
                                  deviceInfo->defaultSampleRate, // Use device's default rate
                                  paFramesPerBufferUnspecified,
                                  paClipOff,
                                  nullptr,
                                  nullptr);
        
        if (err != paNoError) {
            std::cerr << "Error opening stream: " << Pa_GetErrorText(err) << std::endl;
            std::cerr << "Device info:" << std::endl
                     << "  Max channels: " << deviceInfo->maxOutputChannels << std::endl
                     << "  Default sample rate: " << deviceInfo->defaultSampleRate << std::endl
                     << "  Default low latency: " << deviceInfo->defaultLowOutputLatency << std::endl;
            return;
        }

        err = Pa_StartStream(stream);
        if (err != paNoError) {
            std::cerr << "Error starting stream: " << Pa_GetErrorText(err) << std::endl;
            return;
        }

        streamInitialized = true;
        std::cout << "Audio stream initialized successfully" << std::endl;
    }

    std::cout << "Playing sound with buffer size: " << buffer.size() << ", channels: " << channels << std::endl;
    
    // Create a temporary buffer for volume-adjusted samples
    std::vector<float> adjustedBuffer(buffer.size());
    
    // Calculate channel volumes based on balance (-100 to +100)
    // Convert balance to a ratio between 0 and 1
    float balanceRatio = (balance + 100.0f) / 200.0f;
    float leftVolume = volume * (1.0f - balanceRatio);
    float rightVolume = volume * balanceRatio;
    
    // Apply the appropriate scaling factor based on which sound is playing
    float scaling = (buffer.data() == AOA_warning_buffer.data()) ? aoa_warning_scaling : stall_warning_scaling;
    
    std::cout << "Audio parameters - Left vol: " << leftVolume << ", Right vol: " << rightVolume 
              << ", Scaling: " << scaling << std::endl;

    // Apply volume adjustments
    for (size_t i = 0; i < buffer.size(); i += channels) {
        if (channels > 1) {
            adjustedBuffer[i] = buffer[i] * (leftVolume / 100.0f) * scaling;
            adjustedBuffer[i + 1] = buffer[i + 1] * (rightVolume / 100.0f) * scaling;
        } else {
            adjustedBuffer[i] = buffer[i] * (volume / 100.0f) * scaling;
        }
    }

    PaError err = Pa_WriteStream(stream, adjustedBuffer.data(), buffer.size() / channels);
    if (err != paNoError) {
        std::cerr << "Error writing to stream: " << Pa_GetErrorText(err) << std::endl;
    }
}

// Cleanup function to be called at program exit
void cleanupAudio() {
    Pa_Terminate();
}

// Thread function to process sound playback
void soundPlaybackThread() {
    while (true) {
        std::unique_lock<std::mutex> lock(queueMutex);
        queueCondition.wait(lock, [] { return !soundQueue.empty(); });

        while (!soundQueue.empty()) {
            auto [file, volume, balance, deviceIndex] = soundQueue.front();
            soundQueue.pop();
            lock.unlock();

            std::cout << "Processing sound: file=" << file << ", volume=" << volume 
                     << ", balance=" << balance << ", device=" << deviceIndex << std::endl;

            // Remove the audio/ prefix for comparison
            std::string audioFile = file;
            if (audioFile.substr(0, 6) == "audio/") {
                audioFile = audioFile.substr(6);
            }

            if (audioFile == AOA_warning_audio_file) {
                if (!AOA_warning_buffer.empty()) {
                    playPreprocessedSound(AOA_warning_buffer, AOA_warning_sampleRate, 
                                       AOA_warning_channels, deviceIndex, volume, balance);
                } else {
                    std::cerr << "Error: AOA warning buffer is empty" << std::endl;
                }
            } else if (audioFile == Stall_warning_audio_file) {
                if (!Stall_warning_buffer.empty()) {
                    playPreprocessedSound(Stall_warning_buffer, Stall_warning_sampleRate, 
                                       Stall_warning_channels, deviceIndex, volume, balance);
                } else {
                    std::cerr << "Error: Stall warning buffer is empty" << std::endl;
                }
            } else {
                std::cerr << "Error: Unknown audio file: " << file << std::endl;
            }

            lock.lock();
        }
    }
}

int main() {
    std::cout << "Starting program..." << std::endl;

    // Initialize PortAudio library
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "Failed to initialize PortAudio: " << Pa_GetErrorText(err) << std::endl;
        return 1;
    }

    // Register cleanup function to be called at exit
    std::atexit(cleanupAudio);

    // List available audio devices
    listAudioDevices();

    readConfig();

    // Convert device indices to actual WASAPI device indices
    AOA_warning_device_index = findWasapiDevice(AOA_warning_device_index);
    Stall_warning_device_index = findWasapiDevice(Stall_warning_device_index);

    std::cout << "Using WASAPI device indices: AOA=" << AOA_warning_device_index 
              << ", Stall=" << Stall_warning_device_index << std::endl;

    // Preprocess audio data
    preprocessAudioData(AOA_warning_audio_file, AOA_warning_start_volume, AOA_warning_balance, AOA_warning_buffer, AOA_warning_sampleRate, AOA_warning_channels);
    preprocessAudioData(Stall_warning_audio_file, Stall_warning_volume, Stall_warning_balance, Stall_warning_buffer, Stall_warning_sampleRate, Stall_warning_channels);

    // Analyze both audio files and calculate safe scaling factors
    std::cout << "\nAnalyzing warning sound levels..." << std::endl;
    aoa_warning_scaling = analyzeAudioLevels(AOA_warning_buffer, "AOA Warning");
    stall_warning_scaling = analyzeAudioLevels(Stall_warning_buffer, "Stall Warning");
    
    std::cout << "\nFinal scaling factors:" << std::endl;
    std::cout << "AOA Warning scaling: " << aoa_warning_scaling << std::endl;
    std::cout << "Stall Warning scaling: " << stall_warning_scaling << std::endl;

    // Add debug output for buffer state after preprocessing
    std::cout << "Buffer states after preprocessing:" << std::endl;
    std::cout << "AOA Warning buffer size: " << AOA_warning_buffer.size() 
              << ", channels: " << AOA_warning_channels << std::endl;
    std::cout << "Stall Warning buffer size: " << Stall_warning_buffer.size() 
              << ", channels: " << Stall_warning_channels << std::endl;

    // Start sound playback thread
    std::thread playbackThread(soundPlaybackThread);
    playbackThread.detach();

    boost::asio::io_context io_context;
    boost::asio::ip::udp::socket socket(io_context);
    boost::asio::ip::udp::endpoint local_endpoint(boost::asio::ip::udp::v4(), 12345);
    boost::system::error_code ec;
    socket.open(local_endpoint.protocol(), ec);
    if (ec) {
        std::cerr << "Failed to open socket: " << ec.message() << std::endl;
        return 1;
    }
    socket.bind(local_endpoint, ec);
    if (ec) {
        std::cerr << "Failed to bind socket: " << ec.message() << std::endl;
        return 1;
    }
    std::cout << "Socket opened and bound successfully." << std::endl;

    char recv_buf[1024];
    boost::asio::ip::udp::endpoint sender_endpoint;

    while (true) {
        std::cout << "Waiting to receive data..." << std::endl;
        boost::system::error_code error;
        size_t len = socket.receive_from(boost::asio::buffer(recv_buf), sender_endpoint, 0, error);
        if (error && error != boost::asio::error::message_size) {
            std::cerr << "Receive failed: " << error.message() << std::endl;
            break;
        }

        if (len > 0) {
            std::string data(recv_buf, len);
            float IAS, AoA;
            sscanf(data.c_str(), "%f,%f", &IAS, &AoA);
            std::cout << "Received data: IAS=" << IAS << ", AoA=" << AoA << std::endl;

            {
                std::lock_guard<std::mutex> lock(queueMutex);
                if (soundPlaying) {
                    stopCurrentSound = true; // Stop the current sound
                }
                soundQueue = {}; // Clear the sound queue
            }

            if (AoA > AOA_Warning_Start && AoA < Stall_warning) {
                float volume = calculateVolume(AoA, AOA_Warning_Start, AOA_Warning_End, AOA_warning_start_volume, AOA_warning_end_volume);
                std::cout << "Calculated AOA warning volume: " << volume << " for AoA: " << AoA << std::endl;
                {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    soundQueue.emplace("audio/" + AOA_warning_audio_file, volume, AOA_warning_balance, AOA_warning_device_index); // Use relative path
                }
                queueCondition.notify_one();
            } else if (AoA >= Stall_warning) {
                std::cout << "Using stall warning volume: " << Stall_warning_volume << " for AoA: " << AoA << std::endl;
                {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    soundQueue.emplace("audio/" + Stall_warning_audio_file, Stall_warning_volume, Stall_warning_balance, Stall_warning_device_index); // Use relative path
                }
                queueCondition.notify_one();
            }
        }
    }

    std::cout << "Program exiting..." << std::endl;
    return 0;
}
