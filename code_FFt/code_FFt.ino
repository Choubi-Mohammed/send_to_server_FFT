#include <driver/i2s.h>
#include <arduinoFFT.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SPIFFS.h>

// Hardware Configuration
#define I2S_WS_PIN      25
#define I2S_SD_PIN      32
#define I2S_SCK_PIN     33
#define I2S_PORT        I2S_NUM_0

// FFT Configuration
#define SAMPLES         2048        // Keep this for FFT size
#define BUFFER_SIZE     1024        // Matches I2S buffer limit
#define SAMPLING_FREQ   44100

// Target Frequency Range (17.7kHz-18.3kHz)
#define TARGET_FREQ_MIN  17700
#define TARGET_FREQ_MAX  18300

// Detection Threshold (Scaled Magnitude)
#define MAGNITUDE_THRESHOLD 1000

// WiFi Configuration
const char* ssid = "inwi Home 4GA11A20";
const char* password = "42674857";

// Backend Configuration
// Try different IP addresses if one doesn't work
const char* backendUrls[] = {
  "http://192.168.8.109:3000/api/detections",    // ESP32 network gateway
//  "http://192.168.43.137:3000/api/detections", // Computer's IP
//  "http://192.168.43.1:3000/api/detections",   // Router IP (common gateway)
//  "http://192.168.1.100:3000/api/detections"   // Alternative IP (update as needed)
};
const int numBackendUrls = 4;
int currentUrlIndex = 0;
const int connectionTimeout = 5000; // Reduced timeout to 5 seconds
bool serverAvailable = false;
unsigned long lastServerCheckTime = 0;
const unsigned long serverCheckInterval = 30000; // Check server every 30 seconds

// Data storage
bool spiffsAvailable = false;
const char* dataLogFile = "/fft_data.csv";
const int maxStoredDetections = 100;
int storedDetections = 0;

// FFT Variables
double vReal[SAMPLES];
double vImag[SAMPLES];
ArduinoFFT<double> FFT(vReal, vImag, SAMPLES, (double)SAMPLING_FREQ);

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize SPIFFS for data storage with proper error handling
  Serial.println("Initializing SPIFFS...");
  if(!SPIFFS.begin(false)) { // Try without formatting first
    Serial.println("SPIFFS mount failed, trying to format...");
    if(SPIFFS.begin(true)) { // Format and try again
      Serial.println("SPIFFS formatted and mounted successfully");
      spiffsAvailable = true;
    } else {
      Serial.println("SPIFFS initialization failed! Will continue without local storage.");
      spiffsAvailable = false;
    }
  } else {
    Serial.println("SPIFFS initialized successfully");
    spiffsAvailable = true;
  }
  
  // Check for stored data if SPIFFS is available
  if(spiffsAvailable) {
    File dataFile = SPIFFS.open(dataLogFile, "r");
    if(dataFile) {
      storedDetections = countLinesInFile(dataFile);
      Serial.print("Found ");
      Serial.print(storedDetections);
      Serial.println(" stored detections");
      dataFile.close();
    }
  }
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi...");
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
    delay(500);
    Serial.print(".");
    wifiAttempts++;
  }
  
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConnected to WiFi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    
    // Try to find a working server
    checkServerAvailability();
    
    // Try to send stored data if we have a connection and storage
    if(serverAvailable && spiffsAvailable && storedDetections > 0) {
      sendStoredData();
    }
  } else {
    Serial.println("\nFailed to connect to WiFi");
  }
  
  setupI2S();
}

void loop() {
  // Feed the watchdog timer to prevent resets
  yield();
  
  if (readAudioSamples()) {
    performFFT();
    analyzeTargetRange();
  }
  
  // Check WiFi connection periodically and attempt to reconnect if needed
  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 60000) { // Check every minute
    lastWifiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected, attempting to reconnect...");
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(ssid, password);
    }
  }
  
  delay(100);
}

void setupI2S() {
  const i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLING_FREQ,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_SIZE,      // Changed to 1024 to comply with I2S limit
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  
  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK_PIN,
    .ws_io_num = I2S_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD_PIN
  };
  
  i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_start(I2S_PORT);
}

bool readAudioSamples() {
  static int buffer_pos = 0;
  int32_t samples_buffer[BUFFER_SIZE];
  size_t bytes_read = 0;
  
  esp_err_t result = i2s_read(I2S_PORT, samples_buffer, sizeof(samples_buffer), &bytes_read, portMAX_DELAY);
  
  if (result != ESP_OK || bytes_read != sizeof(samples_buffer)) {
    return false;
  }
  
  // Fill the FFT buffer with multiple I2S reads
  for (int i = 0; i < BUFFER_SIZE && buffer_pos < SAMPLES; i++) {
    vReal[buffer_pos] = (double)samples_buffer[i] / 16777216.0;
    vImag[buffer_pos] = 0.0;
    buffer_pos++;
  }
  
  // Only return true when we've collected enough samples
  if (buffer_pos >= SAMPLES) {
    buffer_pos = 0;
    return true;
  }
  return false;
}

void performFFT() {
  FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.compute(FFT_FORWARD);
  FFT.complexToMagnitude();
}

bool checkServerAvailability() {
  // Only check periodically to avoid too many requests
  unsigned long currentTime = millis();
  if (serverAvailable && (currentTime - lastServerCheckTime < serverCheckInterval)) {
    return serverAvailable;
  }
  
  lastServerCheckTime = currentTime;
  serverAvailable = false;
  
  // Try each backend URL until one works
  for (int i = 0; i < numBackendUrls; i++) {
    HTTPClient httpCheck;
    String healthUrl = String(backendUrls[i]).substring(0, String(backendUrls[i]).lastIndexOf("/")) + "/health";
    Serial.print("Checking server at: ");
    Serial.println(healthUrl);
    
    httpCheck.begin(healthUrl);
    httpCheck.setTimeout(connectionTimeout / 2); // Use shorter timeout for checks
    
    int checkCode = httpCheck.GET();
    if (checkCode > 0) {
      Serial.print("Server found at index ");
      Serial.print(i);
      Serial.print(": ");
      Serial.println(backendUrls[i]);
      currentUrlIndex = i;
      serverAvailable = true;
      httpCheck.end();
      break;
    }
    
    Serial.print("Server unavailable at index ");
    Serial.print(i);
    Serial.print(": ");
    Serial.println(httpCheck.errorToString(checkCode));
    httpCheck.end();
  }
  
  return serverAvailable;
}

int countLinesInFile(File file) {
  int count = 0;
  if(!file) return 0;
  
  while(file.available()) {
    String line = file.readStringUntil('\n');
    if(line.length() > 0) {
      count++;
    }
    // Feed the watchdog timer
    yield();
  }
  return count;
}

void storeDetectionLocally(double frequency, double magnitude) {
  if(!spiffsAvailable) {
    Serial.println("SPIFFS not available, can't store detection locally");
    return;
  }
  
  File dataFile = SPIFFS.open(dataLogFile, "a");
  if(!dataFile) {
    Serial.println("Failed to open data file for writing");
    return;
  }
  
  // Format: frequency,magnitude,timestamp
  String dataLine = String(frequency, 0) + "," + 
                   String(magnitude) + "," + 
                   String(millis()) + "\n";
  
  if(dataFile.println(dataLine)) {
    storedDetections++;
    Serial.println("Detection stored locally");
  } else {
    Serial.println("Failed to write detection to file");
  }
  
  dataFile.close();
  
  // If we have too many stored detections, trim the file
  if(storedDetections > maxStoredDetections) {
    trimDataFile();
  }
}

void trimDataFile() {
  if(!spiffsAvailable) {
    return;
  }
  
  File sourceFile = SPIFFS.open(dataLogFile, "r");
  if(!sourceFile) {
    Serial.println("Failed to open data file for reading");
    return;
  }
  
  // Skip the oldest entries
  int linesToSkip = storedDetections - maxStoredDetections;
  for(int i = 0; i < linesToSkip; i++) {
    sourceFile.readStringUntil('\n');
    yield(); // Feed the watchdog timer
  }
  
  // Read the remaining lines
  String remainingData = "";
  while(sourceFile.available()) {
    remainingData += sourceFile.readStringUntil('\n') + "\n";
    yield(); // Feed the watchdog timer
  }
  sourceFile.close();
  
  // Rewrite the file with only the newer entries
  File destFile = SPIFFS.open(dataLogFile, "w");
  if(!destFile) {
    Serial.println("Failed to open data file for writing");
    return;
  }
  
  destFile.print(remainingData);
  destFile.close();
  
  storedDetections = maxStoredDetections;
  Serial.println("Data file trimmed to most recent entries");
}

bool sendStoredData() {
  if(!serverAvailable || !spiffsAvailable) {
    Serial.println("Server or SPIFFS not available to send stored data");
    return false;
  }
  
  File dataFile = SPIFFS.open(dataLogFile, "r");
  if(!dataFile) {
    Serial.println("No stored data found");
    return false;
  }
  
  Serial.println("Sending stored detections...");
  int successCount = 0;
  int failCount = 0;
  
  while(dataFile.available()) {
    String line = dataFile.readStringUntil('\n');
    if(line.length() > 0) {
      // Parse the stored data
      int firstComma = line.indexOf(',');
      int secondComma = line.indexOf(',', firstComma + 1);
      
      if(firstComma > 0 && secondComma > 0) {
        String freqStr = line.substring(0, firstComma);
        String magStr = line.substring(firstComma + 1, secondComma);
        String timeStr = line.substring(secondComma + 1);
        
        double frequency = freqStr.toDouble();
        double magnitude = magStr.toDouble();
        
        // Send to server
        if(sendSingleDetection(frequency, magnitude)) {
          successCount++;
        } else {
          failCount++;
          // If we start failing, stop trying
          if(failCount > 3) {
            break;
          }
        }
      }
    }
    
    // Small delay to avoid overwhelming the server and feed watchdog
    yield();
    delay(50);
  }
  
  dataFile.close();
  
  if(successCount > 0) {
    Serial.print("Successfully sent ");
    Serial.print(successCount);
    Serial.println(" stored detections");
    
    // Clear the file if all were sent successfully
    if(failCount == 0) {
      SPIFFS.remove(dataLogFile);
      storedDetections = 0;
      Serial.println("All stored data sent and cleared");
      return true;
    }
  }
  
  return false;
}

bool sendSingleDetection(double frequency, double magnitude) {
  if(!serverAvailable) {
    return false;
  }
  
  HTTPClient http;
  http.begin(backendUrls[currentUrlIndex]);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(connectionTimeout);

  // Create JSON payload
  String payload = "{\"frequency\":" + String(frequency, 0) + 
                   ",\"magnitude\":" + String(magnitude) + 
                   ",\"timestamp\":" + String(millis()) + "}";

  int httpResponseCode = http.POST(payload);
  bool success = (httpResponseCode > 0);
  http.end();
  
  return success;
}

void sendDetectionToBackend(double frequency, double magnitude) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, storing detection locally");
    storeDetectionLocally(frequency, magnitude);
    return;
  }

  // Check if any server is available
  if (!checkServerAvailability()) {
    Serial.println("No server available, storing detection locally");
    storeDetectionLocally(frequency, magnitude);
    return;
  }

  // Proceed with sending the detection to the working server
  HTTPClient http;
  http.begin(backendUrls[currentUrlIndex]);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(connectionTimeout);

  // Create JSON payload
  String payload = "{\"frequency\":" + String(frequency, 0) + 
                   ",\"magnitude\":" + String(magnitude) + 
                   ",\"timestamp\":" + String(millis()) + "}";

  int httpResponseCode = http.POST(payload);
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.print("Detection sent to backend, response code: ");
    Serial.println(httpResponseCode);
    
    // Try to send any stored data now that we have a connection
    if(storedDetections > 0) {
      sendStoredData();
    }
  } else {
    Serial.print("Error sending detection: ");
    Serial.println(httpResponseCode);
    Serial.println("Error: " + http.errorToString(httpResponseCode));
    
    // Store the detection locally
    Serial.println("Storing detection locally due to send error");
    storeDetectionLocally(frequency, magnitude);
    
    // Mark server as unavailable to trigger a fresh check next time
    serverAvailable = false;
    
    // Try reconnecting to WiFi if connection was lost
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi connection lost. Reconnecting...");
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(ssid, password);
    }
  }
  
  http.end();
}

void analyzeTargetRange() {
  int minBin = round((TARGET_FREQ_MIN * SAMPLES) / SAMPLING_FREQ);
  int maxBin = round((TARGET_FREQ_MAX * SAMPLES) / SAMPLING_FREQ);
  
  if (maxBin >= SAMPLES/2) return;

  double maxMagnitude = 0;
  double peakFreq = 0;
  
  // Find peak frequency in target range
  for (int i = minBin; i <= maxBin; i++) {
    double currentMag = vReal[i];
    if (currentMag > maxMagnitude) {
      maxMagnitude = currentMag;
      peakFreq = (i * SAMPLING_FREQ) / (double)SAMPLES;
    }
  }

  // Calculate scaled magnitude
  int scaledMagnitude = (int)(maxMagnitude * 5000);

  // Only process if magnitude threshold is met
  if ((int)(maxMagnitude * 1000) >= MAGNITUDE_THRESHOLD) {
    Serial.print("Peak Frequency: ");
    Serial.print(peakFreq, 0);
    Serial.print(" Hz\nPeak Magnitude: ");
    Serial.println(scaledMagnitude);
    Serial.println(); // Extra blank line for readability
    
    // Only send detection to backend if magnitude exceeds 20000
    if (scaledMagnitude >= 20000) {
      Serial.println("Magnitude exceeds 20000, sending to server...");
      sendDetectionToBackend(peakFreq, scaledMagnitude);
    }
  }
}
