#include <Arduino.h>
#include "AudioTools.h"
#include "BluetoothA2DPSink.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RotaryEncoder.h>

// Pin definitions
#define DSP_MODEL           DSP_SSD1306
#define I2S_DOUT            25
#define I2S_BCLK            27
#define I2S_LRC             26
#define ENC_BTNR            32
#define ENC_BTNL            33
#define ENC_BTNB            34
#define ENC_INTERNALPULLUP  false
#define ENC2_BTNR           35
#define ENC2_BTNL           36
#define ENC2_BTNB           39
#define ENC2_INTERNALPULLUP false

// OLED display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Global objects
BluetoothA2DPSink a2dp_sink;
I2SStream i2s;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
RotaryEncoder volumeEncoder(ENC_BTNR, ENC_BTNL, RotaryEncoder::LatchMode::TWO03);
RotaryEncoder trackEncoder(ENC2_BTNR, ENC2_BTNL, RotaryEncoder::LatchMode::TWO03);

// Global variables
int volume = 50;                // Volume level (0-100)
int lastVolumeEncoderPos = 0;
int lastTrackEncoderPos = 0;
String deviceName = "ESP32-Speaker";
String connectedDevice = "Not Connected";
String trackTitle = "No Track";
String artist = "Unknown Artist";
bool isPlaying = false;
bool displayNeedsUpdate = true;
bool showVolumeBar = false;     // Only show volume bar during changes
unsigned long lastDisplayUpdate = 0;
unsigned long lastButtonCheck = 0;
unsigned long volumeBarShowTime = 0;
const unsigned long VOLUME_BAR_TIMEOUT = 3000; // Show for 3 seconds

// Function declarations
void setupDisplay();
void setupBluetooth();
void setupEncoders();
void updateDisplay();
void handleVolumeEncoder();
void handleTrackEncoder();
void handleButtons();
void onBluetoothConnected(esp_a2d_connection_state_t state, void* ptr);
void read_data_stream(const uint8_t* data, uint32_t length);
void avrc_metadata_callback(uint8_t id, const uint8_t *text);

void setup() {
    Serial.begin(115200);
    Serial.println("ESP32 Bluetooth Speaker Starting...");
    
    // Initialize display
    setupDisplay();
    
    // Initialize encoders
    setupEncoders();
    
    // Initialize Bluetooth
    setupBluetooth();
    
    Serial.println("Setup complete!");
    displayNeedsUpdate = true;
}

void loop() {
    unsigned long currentTime = millis();
    
    // Handle encoder inputs
    handleVolumeEncoder();
    handleTrackEncoder();
    
    // Handle button inputs (check every 50ms)
    if (currentTime - lastButtonCheck > 50) {
        handleButtons();
        lastButtonCheck = currentTime;
    }
    
    // Update display (every 100ms)
    if (displayNeedsUpdate || (currentTime - lastDisplayUpdate > 100)) {
        updateDisplay();
        lastDisplayUpdate = currentTime;
        displayNeedsUpdate = false;
    }
    
    delay(10); // Small delay to prevent watchdog issues
}

void setupDisplay() {
    Wire.begin(21, 22); // SDA=21, SCL=22 for ESP32
    
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("SSD1306 allocation failed");
        for (;;); // Don't proceed, loop forever
    }
    
    // Clear display and set monochrome white
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK); // White text on black background
    display.setCursor(0, 0);
    display.println("ESP32 BT Speaker");
    display.println("Initializing...");
    display.display();
    
    Serial.println("Display initialized");
}

void setupBluetooth() {
    // Initialize I2S output for PCM5102A DAC
    auto config = i2s.defaultConfig(TX_MODE);
    config.pin_bck = I2S_BCLK;
    config.pin_ws = I2S_LRC;
    config.pin_data = I2S_DOUT;
    config.sample_rate = 44100;
    config.bits_per_sample = 16;
    config.channels = 2;
    i2s.begin(config);
    
    // Initialize Bluetooth A2DP sink with AVRCP support and auto-reconnect
    a2dp_sink.set_stream_reader(read_data_stream, false);
    a2dp_sink.set_on_connection_state_changed(onBluetoothConnected);
    a2dp_sink.set_avrc_metadata_callback(avrc_metadata_callback);
    
    // Enable auto-reconnect and make device discoverable
    a2dp_sink.set_auto_reconnect(true);
    a2dp_sink.start(deviceName.c_str());
    
    // Set initial volume
    a2dp_sink.set_volume(volume);
    
    Serial.println("Bluetooth A2DP initialized with auto-reconnect enabled");
}

void setupEncoders() {
    // Setup volume encoder pins
    if (!ENC_INTERNALPULLUP) {
        pinMode(ENC_BTNR, INPUT_PULLUP);
        pinMode(ENC_BTNL, INPUT_PULLUP);
    }
    pinMode(ENC_BTNB, INPUT_PULLUP);
    
    // Setup track encoder pins
    if (!ENC2_INTERNALPULLUP) {
        pinMode(ENC2_BTNR, INPUT_PULLUP);
        pinMode(ENC2_BTNL, INPUT_PULLUP);
    }
    pinMode(ENC2_BTNB, INPUT_PULLUP);
    
    // Initialize encoder positions
    lastVolumeEncoderPos = volumeEncoder.getPosition();
    lastTrackEncoderPos = trackEncoder.getPosition();
    
    Serial.println("Encoders initialized");
}

void updateDisplay() {
    display.clearDisplay();
    
    // Set monochrome white text
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    
    // Start from top of screen - no title bar
    int currentYPos = 0;
    
    // Connection status
    display.setCursor(0, currentYPos);
    if (a2dp_sink.is_connected()) {
        String deviceText = connectedDevice;
        // Show more of the device name (up to 18 characters)
        if (deviceText.length() > 18) {
            deviceText = deviceText.substring(0, 15) + "...";
        }
        display.println(deviceText);
    } else {
        display.println("Waiting for device...");
    }
    currentYPos += 10;
    
    // Volume section - ALWAYS show volume bar
    display.setCursor(0, currentYPos);
    display.print("Vol: ");
    display.print(volume);
    display.print("%");
    
    // Volume bar visualization - always visible
    int barWidth = 80;
    int barHeight = 6;
    int barX = 50;
    int barY = currentYPos;
    
    // Draw volume bar frame
    display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);
    
    // Fill volume bar
    int fillWidth = (volume * (barWidth - 2)) / 100;
    if (fillWidth > 0) {
        display.fillRect(barX + 1, barY + 1, fillWidth, barHeight - 2, SSD1306_WHITE);
    }
    
    currentYPos += 12; // Move content down
    
    // Track info section - better text fitting
    if (a2dp_sink.is_connected()) {
        if (isPlaying) {
            // Artist name with better word wrapping
            display.setCursor(0, currentYPos);
            String displayArtist = artist;
            if (displayArtist == "Unknown Artist" || displayArtist == "" || displayArtist == "From Phone") {
                displayArtist = "No artist info";
            }
            
            // Better word wrap for artist name (21 chars per line)
            int maxCharsPerLine = 21;
            if (displayArtist.length() > maxCharsPerLine) {
                // Try to break at word boundaries
                int breakPoint = maxCharsPerLine;
                String line1, line2;
                
                // Look for space near the break point
                for (int i = maxCharsPerLine; i >= maxCharsPerLine - 5; i--) {
                    if (i < displayArtist.length() && displayArtist.charAt(i) == ' ') {
                        breakPoint = i;
                        break;
                    }
                }
                
                line1 = displayArtist.substring(0, breakPoint);
                line2 = displayArtist.substring(breakPoint);
                line2.trim(); // Remove leading space
                
                // If second line is still too long, truncate it
                if (line2.length() > maxCharsPerLine) {
                    line2 = line2.substring(0, maxCharsPerLine - 3) + "...";
                }
                
                display.println(line1);
                currentYPos += 10;
                display.setCursor(0, currentYPos);
                display.println(line2);
            } else {
                display.println(displayArtist);
            }
            currentYPos += 10;
            
            // Song title with better word wrapping
            display.setCursor(0, currentYPos);
            String displayTitle = trackTitle;
            if (displayTitle == "No Track" || displayTitle == "Playing Music") {
                displayTitle = "Loading...";
            }
            
            // Improved word wrap for song titles (handles longer titles)
            const int maxChars = 21;
            if (displayTitle.length() > maxChars) {
                // Try to break at word boundaries
                int breakPoint = maxChars;
                String line1, line2;
                
                // Look for space near the break point
                for (int i = maxChars; i >= maxChars - 5; i--) {
                    if (i < displayTitle.length() && displayTitle.charAt(i) == ' ') {
                        breakPoint = i;
                        break;
                    }
                }
                
                line1 = displayTitle.substring(0, breakPoint);
                line2 = displayTitle.substring(breakPoint);
                line2.trim(); // Remove leading space
                
                // If second line is still too long, truncate it
                if (line2.length() > maxChars) {
                    line2 = line2.substring(0, maxChars - 3) + "...";
                }
                
                display.println(line1);
                currentYPos += 10;
                display.setCursor(0, currentYPos);
                display.println(line2);
            } else {
                display.println(displayTitle);
            }
            
        } else {
            display.setCursor(0, currentYPos);
            display.println("Ready - Press Vol knob");
            display.setCursor(0, currentYPos + 10);
            display.println("to Play/Pause");
        }
    } else {
        display.setCursor(0, currentYPos);
        display.println("Pair your device");
        display.setCursor(0, currentYPos + 10);
        display.println("Name: ESP32-Speaker");
    }
    
    display.display();
}

void handleVolumeEncoder() {
    volumeEncoder.tick();
    int newPos = volumeEncoder.getPosition();
    
    if (newPos != lastVolumeEncoderPos) {
        int direction = newPos - lastVolumeEncoderPos;
        // Reverse direction to match standard AV stereo controls
        // Clockwise (right turn) should increase volume
        volume -= direction * 5; // Reversed: subtract instead of add
        
        // Constrain volume to 0-100
        volume = constrain(volume, 0, 100);
        
        // Set Bluetooth volume
        a2dp_sink.set_volume(volume);
        
        lastVolumeEncoderPos = newPos;
        displayNeedsUpdate = true;
        
        Serial.print("Volume: ");
        Serial.print(volume);
        Serial.println("%");
    }
}

void handleTrackEncoder() {
    trackEncoder.tick();
    int newPos = trackEncoder.getPosition();
    
    if (newPos != lastTrackEncoderPos) {
        int direction = newPos - lastTrackEncoderPos;
        
        if (a2dp_sink.is_connected()) {
            if (direction > 0) {
                // Next track (clockwise)
                Serial.println("Next track command sent");
                a2dp_sink.next();
                displayNeedsUpdate = true;
            } else {
                // Previous track (counter-clockwise)
                Serial.println("Previous track command sent");
                a2dp_sink.previous();
                displayNeedsUpdate = true;
            }
        } else {
            Serial.println("Track control: No device connected");
        }
        
        lastTrackEncoderPos = newPos;
    }
}

void handleButtons() {
    // Volume encoder button (play/pause)
    static bool lastVolumeButton = HIGH;
    bool volumeButton = digitalRead(ENC_BTNB);
    
    if (volumeButton == LOW && lastVolumeButton == HIGH) {
        if (a2dp_sink.is_connected()) {
            Serial.println("Play/Pause button pressed");
            // Toggle play/pause state
            static bool isPaused = false;
            if (isPaused) {
                a2dp_sink.play();
                isPaused = false;
            } else {
                a2dp_sink.pause();
                isPaused = true;
            }
            displayNeedsUpdate = true;
        } else {
            Serial.println("Play/Pause: No device connected");
        }
    }
    lastVolumeButton = volumeButton;
    
    // Track encoder button (stop)
    static bool lastTrackButton = HIGH;
    bool trackButton = digitalRead(ENC2_BTNB);
    
    if (trackButton == LOW && lastTrackButton == HIGH) {
        if (a2dp_sink.is_connected()) {
            Serial.println("Stop button pressed");
            a2dp_sink.stop();
            isPlaying = false;
            displayNeedsUpdate = true;
        } else {
            Serial.println("Stop: No device connected");
        }
    }
    lastTrackButton = trackButton;
}

void onBluetoothConnected(esp_a2d_connection_state_t state, void* ptr) {
    if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
        // Try to get device name or use default
        connectedDevice = "Phone Connected";
        isPlaying = false;
        Serial.print("Bluetooth device connected: ");
        Serial.println(connectedDevice);
    } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        connectedDevice = "Not Connected";
        isPlaying = false;
        trackTitle = "No Track";
        artist = "Unknown Artist";
        Serial.println("Bluetooth device disconnected");
    }
    displayNeedsUpdate = true;
}

void read_data_stream(const uint8_t* data, uint32_t length) {
    // This function is called when audio data is received
    // Write the audio data to I2S output
    i2s.write(data, length);
    
    if (!isPlaying) {
        isPlaying = true;
        displayNeedsUpdate = true;
        Serial.println("Audio stream started");
    }
}

void avrc_metadata_callback(uint8_t id, const uint8_t *text) {
    // This function receives metadata from the connected device
    String metadata = String((char*)text);
    
    switch (id) {
        case ESP_AVRC_MD_ATTR_TITLE:
            // Clean up YouTube titles - remove channel names
            trackTitle = metadata;
            
            // Remove common YouTube patterns
            if (trackTitle.indexOf(" - ") > 0) {
                // If there's " - ", keep only the part after it (usually the song name)
                int dashPos = trackTitle.lastIndexOf(" - ");
                if (dashPos > 0 && dashPos < trackTitle.length() - 3) {
                    trackTitle = trackTitle.substring(dashPos + 3);
                }
            }
            
            // Remove "(Official Video)", "(Official Music Video)", etc.
            trackTitle.replace("(Official Video)", "");
            trackTitle.replace("(Official Music Video)", "");
            trackTitle.replace("(Official Audio)", "");
            trackTitle.replace("(Lyric Video)", "");
            trackTitle.replace("(Lyrics)", "");
            trackTitle.replace("[Official Video]", "");
            trackTitle.replace("[Official Music Video]", "");
            trackTitle.replace("[Official Audio]", "");
            trackTitle.replace("[Lyric Video]", "");
            trackTitle.replace("[Lyrics]", "");
            
            // Trim whitespace
            trackTitle.trim();
            
            Serial.print("Clean Track Title: ");
            Serial.println(trackTitle);
            break;
            
        case ESP_AVRC_MD_ATTR_ARTIST:
            artist = metadata;
            
            // Clean up artist name - remove "VEVO", "Records", etc.
            artist.replace("VEVO", "");
            artist.replace("Records", "");
            artist.replace("Music", "");
            artist.replace(" - Topic", "");
            artist.trim();
            
            Serial.print("Clean Artist: ");
            Serial.println(artist);
            break;
            
        case ESP_AVRC_MD_ATTR_ALBUM:
            Serial.print("Album: ");
            Serial.println(metadata);
            break;
        case ESP_AVRC_MD_ATTR_TRACK_NUM:
            Serial.print("Track Number: ");
            Serial.println(metadata);
            break;
        case ESP_AVRC_MD_ATTR_NUM_TRACKS:
            Serial.print("Total Tracks: ");
            Serial.println(metadata);
            break;
        case ESP_AVRC_MD_ATTR_GENRE:
            Serial.print("Genre: ");
            Serial.println(metadata);
            break;
        case ESP_AVRC_MD_ATTR_PLAYING_TIME:
            Serial.print("Playing Time: ");
            Serial.println(metadata);
            break;
        default:
            Serial.print("Unknown metadata (ID ");
            Serial.print(id);
            Serial.print("): ");
            Serial.println(metadata);
            break;
    }
    
    displayNeedsUpdate = true;
}