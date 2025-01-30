DCS Haptic: Angle of Attack Warning 

This program provides haptic feedback to warn pilots of high angles of attack (AoA) in DCS World. It features two customizable levels:
- Warning Interval: Helps develop muscle memory for gauging turn intensity.
- Stall Warning: Alerts the pilot when approaching critical AoA.
Both levels are adjustable via configuration files in the "configuration" folder.

Setup and Configuration

Audio Device Selection
When launching DCS Haptic, available sound devices are enumerated. Use the displayed numbers to easily configure your preferred audio output in the configuration file.

Customizable Audio
Audio files are located in the "audio" folder. You can use custom sounds by adding them to the audio folder and modifying the configuration file accordingly.

Module-Specific Configuration

The first time you fly a new module, a configuration file named after the airframe is created in the "configuration" folder. Initial settings are based on "default.cfg", so customize this file to your preferences first.

DCS Integration
To enable telemetry data transmission from DCS:
Open the "scripts/export.lua" file from the release zip.
Copy the text found in the file to the end of your "Saved Games/DCS.../Scripts/export.lua" file.
Copy "scripts/AOAHaptic.lua" to your "Saved Games/DCS.../Scripts/" folder.

Usage
Start the program before launching DCS World to activate the haptic feedback.
