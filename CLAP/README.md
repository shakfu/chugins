# CLAP Chugin

A ChucK chugin that allows loading and using CLAP (CLever Audio Plugin) plugins in ChucK.

## Features

- Load CLAP plugins by file path
- Support for both effect and instrument plugins
- Parameter control by index and by name
- MIDI support for CLAP instruments (note on/off)
- Bypass functionality
- Cross-platform support (macOS, Windows, Linux)
- **No external SDK required** - CLAP headers included in repository

## What is CLAP?

CLAP (CLever Audio Plugin) is a modern, open-source audio plugin standard developed by the audio plugin community. Key features:

- **Open Source** - Free and open specification
- **Modern Design** - Built for current and future audio production needs
- **Extensible** - Modular extension system
- **Cross-Platform** - Works on macOS, Windows, and Linux
- **No Licensing Fees** - Completely free to use

Learn more at [cleveraudio.org](https://cleveraudio.org/)

## Requirements

### Building

The CLAP headers are included in the repository, so no external dependencies are required!

The headers are automatically cloned from the official CLAP repository during the build process.

## Building

### macOS

```bash
cd CLAP
make mac
sudo make install
```

### Linux

```bash
cd CLAP
make linux
sudo make install
```

### Windows

#### Using Visual Studio

1. Open `CLAP.vcxproj` in Visual Studio
2. Build the solution

#### Using nmake

```cmd
nmake -f makefile.win
```

## Usage

### Loading a CLAP Plugin

```chuck
// Create CLAP instance
adc => CLAP clap => dac;

// Load a plugin by file path
clap.load("/Library/Audio/Plug-Ins/CLAP/YourPlugin.clap");
```

### Parameter Control

```chuck
// Get parameter count
clap.paramCount() => int count;

// Get parameter info
for (0 => int i; i < count; i++) {
    <<< "Parameter", i, ":", clap.paramName(i), "=", clap.getParam(i) >>>;
}

// Set parameter by index
clap.setParam(0, 0.5);

// Set parameter by name
clap.setParamByName("Cutoff", 1000.0);

// Get parameter by name
clap.getParamByName("Resonance") => float resonance;
```

### MIDI (for instruments)

```chuck
// Check if plugin is an instrument
if (clap.isInstrument()) {
    // Play a note
    clap.noteOn(60, 100);  // Middle C, velocity 100
    1::second => now;
    clap.noteOff(60);
}
```

### Bypass

```chuck
// Bypass the plugin (audio passes through unchanged)
clap.bypass(1);

// Re-enable the plugin
clap.bypass(0);
```

### Cleanup

```chuck
// Close the plugin when done
clap.close();
```

## Examples

See `CLAP-test.ck` for comprehensive usage examples.

## CLAP Plugin Locations

### macOS
- `/Library/Audio/Plug-Ins/CLAP/` (system-wide)
- `~/Library/Audio/Plug-Ins/CLAP/` (user)

### Windows
- `C:\Program Files\Common Files\CLAP\`
- `%LOCALAPPDATA%\Programs\Common\CLAP\`

### Linux
- `/usr/lib/clap/`
- `/usr/local/lib/clap/`
- `~/.clap/`

### Environment Variable

You can also set the `CLAP_PATH` environment variable to add additional search directories:

```bash
export CLAP_PATH=/path/to/my/clap/plugins
```

## API Reference

### Methods

#### Loading
- `int load(string path)` - Load a CLAP plugin by file path
- `void close()` - Close the currently loaded plugin
- `void list()` - List all available CLAP plugins (static method)

#### Parameters
- `int paramCount()` - Get number of parameters
- `string paramName(int index)` - Get parameter name by index
- `float getParam(int index)` - Get parameter value by index
- `void setParam(int index, float value)` - Set parameter value by index
- `float getParamByName(string name)` - Get parameter value by name
- `int setParamByName(string name, float value)` - Set parameter value by name (returns 1 on success)

#### MIDI (for instruments)
- `int isInstrument()` - Check if plugin is an instrument
- `int noteOn(int pitch, int velocity)` - Send MIDI note on
- `int noteOff(int pitch)` - Send MIDI note off
- `int sendMIDI(int status, int data1, int data2)` - Send raw MIDI message

#### Other
- `void bypass(int bypass)` - Bypass the plugin (1 = bypass, 0 = active)

## Free CLAP Plugins to Try

Here are some excellent free CLAP plugins you can use with this chugin:

### Instruments
- **[Surge XT](https://surge-synthesizer.github.io/)** - Hybrid synthesizer with wavetable, subtractive, and FM synthesis
- **[Vital](https://vital.audio/)** - Modern wavetable synthesizer
- **[Dexed](https://asb2m10.github.io/dexed/)** - FM synthesizer (Yamaha DX7 clone)
- **[Cardinal](https://cardinal.kx.studio/)** - Modular synthesizer (VCV Rack)

### Effects
- **[Airwindows](https://www.airwindows.com/)** - Large collection of unique effects
- **[Chow Tape Model](https://chowdsp.com/products.html)** - Analog tape emulation
- **[Dragonfly Reverb](https://michaelwillis.github.io/dragonfly-reverb/)** - High-quality reverb
- **[Surge XT Effects](https://surge-synthesizer.github.io/)** - Individual effects from Surge XT

Most of these plugins are available in CLAP format or are adding CLAP support.

## Troubleshooting

### Build Issues

**Problem**: CLAP headers not found
- **Solution**: The headers should be automatically cloned during build. If not, manually run:
  ```bash
  cd CLAP
  git clone https://github.com/free-audio/clap.git clap-headers
  ```

### Runtime Issues

**Problem**: Plugin fails to load
- **Solution**: Verify the plugin path is correct and the .clap file exists
- **Solution**: Make sure the plugin is compatible with your system architecture

**Problem**: No audio output from instrument
- **Solution**: Send MIDI note on messages: `clap.noteOn(60, 100)`
- **Solution**: Check if `clap.isInstrument()` returns true

**Problem**: No plugins found with `CLAP.list()`
- **Solution**: Install CLAP plugins in standard locations
- **Solution**: Set `CLAP_PATH` environment variable to your plugin directory

## Advantages of CLAP

Compared to other plugin formats:

### vs VST3
- **No SDK required** - CLAP headers are open source
- **No licensing restrictions** - Completely free
- **Modern design** - Built from scratch for modern workflows
- **Better thread safety** - Improved multi-threading support

### vs AudioUnit
- **Cross-platform** - Works on Windows and Linux too
- **Open standard** - Not tied to a single vendor
- **Extensible** - Easy to add new features

### vs LV2
- **Simpler** - Easier to implement and use
- **Better documentation** - More modern documentation
- **Growing ecosystem** - Rapidly increasing plugin support

## Contributing

CLAP is an open-source standard. To contribute:
- [CLAP Specification](https://github.com/free-audio/clap)
- [CLAP Helpers](https://github.com/free-audio/clap-helpers)
- [Community Forum](https://github.com/free-audio/clap/discussions)

## License

This chugin follows the same license as ChucK (GPL v2).

The CLAP headers are licensed under the MIT License.

## See Also

- [VST3 chugin](../VST3/) - Similar functionality for VST3 plugins
- [AudioUnit chugin](../AudioUnit/) - Similar functionality for macOS AudioUnit plugins
- [ChucK Documentation](https://chuck.cs.princeton.edu/doc/)
- [CLAP Website](https://cleveraudio.org/)
- [CLAP GitHub](https://github.com/free-audio/clap)
