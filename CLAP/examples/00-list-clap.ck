// List all available CLAP plugins on the system
// This is useful for discovering what CLAP plugins are available

// Create CLAP instance
CLAP clap => blackhole;

<<< "=== Listing all available CLAP plugins on your system ===" >>>;

// This will print all available CLAP plugins with their paths
clap.list();

<<< "=== End of CLAP plugin list ===" >>>;
<<< "You can use any of these CLAP plugins in your ChucK programs!" >>>;
<<< "Load by path: clap.load(\"/path/to/plugin.clap\")" >>>;
