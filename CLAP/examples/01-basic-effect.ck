// Basic CLAP Effect Example
// Demonstrates loading and using a CLAP plugin as an audio effect

// Create a CLAP instance and connect it in the audio chain
adc => CLAP clap => dac;

<<< "=== Basic CLAP Effect Example ===" >>>;

// Replace this path with an actual CLAP plugin on your system
// Common CLAP plugin locations:
// macOS: /Library/Audio/Plug-Ins/CLAP/ or ~/Library/Audio/Plug-Ins/CLAP/
// Windows: C:\Program Files\Common Files\CLAP\
// Linux: /usr/lib/clap/ or ~/.clap/

// Example: Using a delay effect (adjust path to match your system)
"/Library/Audio/Plug-Ins/CLAP/ExampleDelay.clap" => string pluginPath;

// Load the CLAP plugin by file path
if (clap.load(pluginPath)) {
    <<< "Successfully loaded CLAP plugin!" >>>;
    <<< "Processing audio from ADC through CLAP for 5 seconds..." >>>;

    // Process audio for 5 seconds
    5::second => now;

    <<< "Closing CLAP plugin..." >>>;
    clap.close();
} else {
    <<< "Failed to load CLAP plugin at:", pluginPath >>>;
    <<< "Please update pluginPath with a valid CLAP plugin on your system" >>>;
    <<< "Run examples/00-list-clap.ck to see available plugins" >>>;
}

<<< "=== Example complete ===" >>>;
