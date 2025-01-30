This is a simple program that creates haptic feedback as warning of high angle of attack. 
It has two levels a warning interval that is intended to help develope a muscle memory in order to be able to tell how hard you are turning. 
It also has a Stall warning level, thet also is based on AoA. Both of these levels are customizable in the config files found in the configuration folder. 

When starting DCS Haptic, you can see the enumeration of the available sound devices. Use this nnumber to easily configure what sound device to use. 
The audio files used are also customizable and can be found in the audio folder. You can make your own if you want to and configure this in the config file.

The first time you fly a module, a new configuration file is created in the configuration folder. The new configuration file will carry the same name as the respective airframe. 
All settings when it creates a new configuration file is based on default.cfg, so first of you should set this to your likings. 

To get DCS to send the telemetric data you need to add the content in scripts/export.lua. Open this file found in the zip file in the relase, and copy the content of 
that file and paste it in the end of your "Saved Games/DCS.../Scripts/export.lua". 
The copy the scripts/AOAHaptic.lua to the "Saved Games/DCS.../Scripts/" folder. 

All done. Just start the program before DCS and you are ready to go. 
