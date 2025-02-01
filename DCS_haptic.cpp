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
#include <filesystem>
#include <atomic>

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

// Add these global variables after the existing globals
std::atomic<bool> shouldStop{false};
std::string currentConfigPath;
std::filesystem::file_time_type lastConfigModTime;
std::string currentAirframe;  // Add this line after other global declarations

// Add these declarations after other global variables
std::string AOA_warning_device_name, Stall_warning_device_name;

// Function to copy default config to new airframe config
bool createAirframeConfig(const std::string& airframeName) {
    std::string defaultPath = "configuration/default.cfg";
    std::string airframePath = "configuration/" + airframeName + ".cfg";
    
    std::ifstream src(defaultPath, std::ios::binary);
    if (!src.is_open()) {
        std::cerr << "Failed to open default configuration file." << std::endl;
        return false;
    }
    
    std::ofstream dst(airframePath, std::ios::binary);
    if (!dst.is_open()) {
        std::cerr << "Failed to create airframe configuration file." << std::endl;
        return false;
    }
    
    dst << src.rdbuf();
    std::cout << "Created new configuration file for " << airframeName << std::endl;
    return true;
}

// Add this new function before readConfig()
std::string findAndUpdateDeviceName(int deviceIndex, const std::string& configPath) {
    const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(deviceIndex);
    if (!deviceInfo) {
        std::cerr << "Invalid device index: " << deviceIndex << std::endl;
        return "";
    }

    std::string deviceName = deviceInfo->name;
    
    // Update the config file to store the device name
    std::ifstream inFile(configPath);
    std::string content((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    inFile.close();

    // Replace the device index with the device name in the config
    size_t pos;
    std::string indexStr = std::to_string(deviceIndex);
    
    // Update AOA warning device
    std::string searchStr = "AOA_warning_device_index=" + indexStr;
    if ((pos = content.find(searchStr)) != std::string::npos) {
        content.replace(pos, searchStr.length(), "AOA_warning_device_name=" + deviceName);
    }
    
    // Update Stall warning device
    searchStr = "Stall_warning_device_index=" + indexStr;
    if ((pos = content.find(searchStr)) != std::string::npos) {
        content.replace(pos, searchStr.length(), "Stall_warning_device_name=" + deviceName);
    }

    std::ofstream outFile(configPath);
    outFile << content;
    outFile.close();

    return deviceName;
}

// Add this new function before findDeviceByName()
int getWasapiDeviceCount() {
    int numDevices = Pa_GetDeviceCount();
    int wasapiCount = 0;
    for (int i = 0; i < numDevices; ++i) {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
        if (deviceInfo && deviceInfo->maxOutputChannels > 0) {
            const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
            if (std::string(hostApiInfo->name).find("WASAPI") != std::string::npos) {
                wasapiCount++;
            }
        }
    }
    return wasapiCount;
}

// Replace existing findDeviceByName() with this version
int findDeviceByName(const std::string& deviceName) {
    int numDevices = Pa_GetDeviceCount();
    // First try to find WASAPI device with this name
    for (int i = 0; i < numDevices; ++i) {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
        if (deviceInfo && deviceInfo->maxOutputChannels > 0) {
            const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
            if (std::string(hostApiInfo->name).find("WASAPI") != std::string::npos) {
                if (std::string(deviceInfo->name) == deviceName) {
                    return i;
                }
            }
        }
    }
    return -1;
}

// Replace existing findWasapiDevice() with this version
int findWasapiDevice(int requestedIndex) {
    int numDevices = Pa_GetDeviceCount();
    int wasapiCount = 0;
    
    // First try to find the nth WASAPI device
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

    // If requested index is too high, return the first WASAPI device
    for (int i = 0; i < numDevices; ++i) {
        const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(i);
        const PaHostApiInfo* hostApiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
        
        if (deviceInfo->maxOutputChannels > 0 && 
            std::string(hostApiInfo->name).find("WASAPI") != std::string::npos) {
            std::cout << "Warning: Requested WASAPI device index " << requestedIndex 
                     << " not found, using first available WASAPI device instead" << std::endl;
            return i;
        }
    }

    std::cerr << "Error: No WASAPI devices found" << std::endl;
    return -1;
}

// Modified readConfig function to handle airframe-specific configs
void readConfig(const std::string& airframeName = "") {
    if (airframeName.empty()) {
        currentConfigPath = "configuration/default.cfg";
    } else {
        currentConfigPath = "configuration/" + airframeName + ".cfg";
        std::ifstream test(currentConfigPath);
        if (!test.is_open()) {
            std::cout << "No configuration found for " << airframeName << ", creating new config..." << std::endl;
            if (createAirframeConfig(airframeName)) {
                std::cout << "Successfully created configuration for " << airframeName << std::endl;
            } else {
                std::cout << "Failed to create airframe config, using default.cfg" << std::endl;
                currentConfigPath = "configuration/default.cfg";
            }
        }
    }
    
    // Update last modification time
    try {
        lastConfigModTime = std::filesystem::last_write_time(currentConfigPath);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error getting file modification time: " << e.what() << std::endl;
    }

    std::ifstream config(currentConfigPath);
    if (!config.is_open()) {
        std::cerr << "Failed to open configuration file: " << currentConfigPath << std::endl;
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
                else if (key == "AOA_warning_device_index") {
                    AOA_warning_device_index = std::stoi(value);
                    AOA_warning_device_name = findAndUpdateDeviceName(AOA_warning_device_index, currentConfigPath);
                }
                else if (key == "AOA_warning_device_name") {
                    AOA_warning_device_name = value;
                    int deviceIndex = findDeviceByName(value);
                    if (deviceIndex >= 0) {
                        AOA_warning_device_index = deviceIndex;
                    }
                }
                else if (key == "AOA_warning_balance") AOA_warning_balance = std::stoi(value);
                else if (key == "Stall_warning_device_index") {
                    Stall_warning_device_index = std::stoi(value);
                    Stall_warning_device_name = findAndUpdateDeviceName(Stall_warning_device_index, currentConfigPath);
                }
                else if (key == "Stall_warning_device_name") {
                    Stall_warning_device_name = value;
                    int deviceIndex = findDeviceByName(value);
                    if (deviceIndex >= 0) {
                        Stall_warning_device_index = deviceIndex;
                    }
                }
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

    // Add device name logging
    std::cout << "AOA Warning Device: " << AOA_warning_device_name 
              << " (index: " << AOA_warning_device_index << ")" << std::endl;
    std::cout << "Stall Warning Device: " << Stall_warning_device_name 
              << " (index: " << Stall_warning_device_index << ")" << std::endl;
}

// Modify listAudioDevices() to show WASAPI indices
void listAudioDevices() {
    Pa_Initialize();
    int numDevices = Pa_GetDeviceCount();
    const PaDeviceInfo* deviceInfo;
    const PaHostApiInfo* hostApiInfo;
    int wasapiCount = 0;
    
    std::cout << "\nAvailable WASAPI output devices:" << std::endl;
    std::cout << "--------------------------------" << std::endl;
    
    for (int i = 0; i < numDevices; ++i) {
        deviceInfo = Pa_GetDeviceInfo(i);
        if (deviceInfo->maxOutputChannels > 0) {
            hostApiInfo = Pa_GetHostApiInfo(deviceInfo->hostApi);
            if (std::string(hostApiInfo->name).find("WASAPI") != std::string::npos) {
                std::cout << "  WASAPI[" << wasapiCount << "] System[" << i << "] " 
                         << deviceInfo->name 
                         << " - Channels: " << deviceInfo->maxOutputChannels
                         << std::endl;
                wasapiCount++;
            }
        }
    }
    
    std::cout << "\nNote: Use the WASAPI[n] index in your config file\n" << std::endl;
    
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
    std::string filePath = "audio/" + file;
    std::cout << "Loading audio file: " << filePath << std::endl;
    
    // Clear the buffer before loading new data
    buffer.clear();
    
    if (!loadAudioData(filePath, buffer, sampleRate, channels)) {
        std::cerr << "Failed to load audio data: " << filePath << std::endl;
        return;
    }

    std::cout << "Successfully loaded audio file: " << filePath << std::endl;
    std::cout << "Initial buffer size: " << buffer.size() << ", channels: " << channels << std::endl;

    if (buffer.empty()) {
        std::cerr << "Error: Buffer is empty after loading" << std::endl;
        return;
    }

    float leftVolume = volume * (1.0f - balance / 100.0f);
    float rightVolume = volume * (1.0f + balance / 100.0f);

    std::cout << "Applying volume adjustments - Left: " << leftVolume << ", Right: " << rightVolume << std::endl;

    // Create a temporary buffer for the processed audio
    std::vector<float> processedBuffer = buffer;

    for (size_t i = 0; i < processedBuffer.size(); i += channels) {
        processedBuffer[i] *= leftVolume / 100.0f;
        if (channels > 1) {
            processedBuffer[i + 1] *= rightVolume / 100.0f;
        }
    }

    // Apply limiter to the audio signal
    applyLimiter(processedBuffer, 0.9f);

    // Only update the buffer if processing was successful
    buffer = std::move(processedBuffer);

    std::cout << "Final buffer size: " << buffer.size() 
              << ", First few samples: " << buffer[0];
    if (buffer.size() > 1) std::cout << ", " << buffer[1];
    if (buffer.size() > 2) std::cout << ", " << buffer[2];
    std::cout << std::endl;
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
    float safeScaling = (maxPeak > 0.0f) ? std::min(maxDesiredPeak / maxPeak, 1.0f) : 1.0f;
    
    std::cout << warningType << " audio analysis - Max peak: " << maxPeak 
              << ", Safe scaling factor: " << safeScaling << std::endl;
    
    return safeScaling;
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

// Add this function before main()
void monitorConfigFile() {
    using namespace std::chrono_literals;
    
    while (!shouldStop) {
        try {
            auto currentModTime = std::filesystem::last_write_time(currentConfigPath);
            if (currentModTime != lastConfigModTime) {
                std::cout << "Configuration file changed, reloading settings..." << std::endl;
                readConfig(currentAirframe);  // Use currentAirframe instead of currentConfigPath
                lastConfigModTime = currentModTime;
                
                // Reload audio buffers with new settings
                preprocessAudioData(AOA_warning_audio_file, AOA_warning_start_volume, 
                                 AOA_warning_balance, AOA_warning_buffer, 
                                 AOA_warning_sampleRate, AOA_warning_channels);
                                 
                preprocessAudioData(Stall_warning_audio_file, Stall_warning_volume, 
                                 Stall_warning_balance, Stall_warning_buffer,
                                 Stall_warning_sampleRate, Stall_warning_channels);
            }
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error monitoring config file: " << e.what() << std::endl;
        }
        
        // Sleep for 5 seconds before next check
        std::this_thread::sleep_for(5s);
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

    // Preprocess audio data with additional error checking
    std::cout << "\nPreprocessing AOA warning audio..." << std::endl;
    preprocessAudioData(AOA_warning_audio_file, AOA_warning_start_volume, 
                       AOA_warning_balance, AOA_warning_buffer, 
                       AOA_warning_sampleRate, AOA_warning_channels);
    
    if (AOA_warning_buffer.empty()) {
        std::cerr << "Error: AOA warning buffer is empty after preprocessing" << std::endl;
        return 1;
    }

    std::cout << "\nPreprocessing Stall warning audio..." << std::endl;
    preprocessAudioData(Stall_warning_audio_file, Stall_warning_volume, 
                       Stall_warning_balance, Stall_warning_buffer,
                       Stall_warning_sampleRate, Stall_warning_channels);
    
    if (Stall_warning_buffer.empty()) {
        std::cerr << "Error: Stall warning buffer is empty after preprocessing" << std::endl;
        return 1;
    }

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

    // Start configuration file monitoring thread
    std::thread configMonitor(monitorConfigFile);
    configMonitor.detach();

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
            char airframe[256];
            sscanf(data.c_str(), "%f,%f,%255s", &IAS, &AoA, airframe);
            std::cout << "Received data: IAS=" << IAS << ", AoA=" << AoA << ", Airframe=" << airframe << std::endl;

            // Reload configuration if airframe changes
            if (currentAirframe != airframe) {
                std::cout << "Airframe changed from '" << currentAirframe << "' to '" << airframe << "'" << std::endl;
                currentAirframe = airframe;
                readConfig(currentAirframe);

                // Reload audio buffers with new configuration
                std::cout << "Reloading audio buffers for new airframe..." << std::endl;
                
                // Clear existing buffers
                AOA_warning_buffer.clear();
                Stall_warning_buffer.clear();
                
                // Preprocess audio data with the new configuration
                preprocessAudioData(AOA_warning_audio_file, AOA_warning_start_volume, 
                                 AOA_warning_balance, AOA_warning_buffer, 
                                 AOA_warning_sampleRate, AOA_warning_channels);
                
                preprocessAudioData(Stall_warning_audio_file, Stall_warning_volume, 
                                 Stall_warning_balance, Stall_warning_buffer,
                                 Stall_warning_sampleRate, Stall_warning_channels);
                
                // Recalculate scaling factors
                aoa_warning_scaling = analyzeAudioLevels(AOA_warning_buffer, "AOA Warning");
                stall_warning_scaling = analyzeAudioLevels(Stall_warning_buffer, "Stall Warning");
                
                std::cout << "Audio buffers reloaded for " << currentAirframe << std::endl;
            }

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

    // When exiting, stop the monitoring thread
    shouldStop = true;

    return 0;
}
