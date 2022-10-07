/*********
  Rui Santos
  Complete project details at https://RandomNerdTutorials.com/esp32-cam-take-photo-display-web-server/
  
  IMPORTANT!!! 
   - Select Board "AI Thinker ESP32-CAM"
   - GPIO 0 must be connected to GND to upload a sketch
   - After connecting GPIO 0 to GND, press the ESP32-CAM on-board RESET button to put your board in flashing mode
  
  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.
*********/

#include "WiFi.h"
#include "esp_camera.h"
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "soc/soc.h"           // Disable brownour problems
#include "soc/rtc_cntl_reg.h"  // Disable brownour problems
#include "driver/rtc_io.h"
#include "soc/rtc.h"
#include "HX711.h"
#include "time.h"
#include <ESPAsyncWebServer.h>
#include <StringArray.h>
#include <SPIFFS.h>
#include <FS.h>
#include "SPI.h"
#include "SPIFFS.h"
#include "ESP32_MailClient.h"

// Replace with your network credentials

const char* ssid = "_Free_Wifi_Berlin";
const char* password = "";
float weight = 0.0;

// define GPIOs for data and clock PIN

const int LOADCELL_DOUT_PIN = 15;
const int LOADCELL_SCK_PIN = 14;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// make variables for time

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;

// control variables for methods

boolean takeNewPhoto = false;
boolean takeNewScaleMeasurement = false;
boolean scaleCalibrated = false;
boolean delete_weight_file = false;

File weights;

// Photo File Name to save in SPIFFS

#define FILE_PHOTO "/photo.jpg"

// OV2640 camera module pins (CAMERA_MODEL_AI_THINKER)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define FLASH_GPIO_NUM    4

#define sender_email    "florian@hybridartlab.com"
#define sender_email_password   "HALH4w4i1s6H4v4h6HAL"
#define SMTP_Server            "smtp.gmail.com"
#define SMTP_Server_Port        465
#define email_subject          "XHab-Image"
#define email_recipient        "flofa@posteo.de"

SMTPData smtpData;
HX711 scale;

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { text-align:center; }
    .vert { margin-bottom: 10%; }
    .hori{ margin-bottom: 0%; }
  </style>
</head>
<body>
  <div id="container">
    <h2>Last X-Hab Photo</h2>
    <p>It might take more than 5 seconds to capture a photo.</p>
    <p>
      <button onclick="rotatePhoto()">ROTATE</button>
      <button onclick="capturePhoto()">CAPTURE PHOTO</button>
      <button onclick="measureWeight()">GET WEIGHT</button>
      <button onclick="sendPhoto()">SEND DATA</button>
      <button onclick="location.reload()">REFRESH PAGE</button>
    </p>
  </div>
  <div><img src="saved-photo" id="photo" width="70%"></div>
  <h3>last measurements of the scale</h3>
  <div id="list"><p><iframe src="measure" id="table" width=600 height=600 frameborder=0 ></iframe></p></div>
</body>
<script>
  var deg = 0;
  function capturePhoto() {
    var xhr = new XMLHttpRequest();
    xhr.open('GET', "/capture", true);
    xhr.send();
  }
  function sendPhoto() {
    var xhr = new XMLHttpRequest();
    xhr.open('GET', "/sendPhoto", true);
    xhr.send();
  }
  function measureWeight() {
    var xhr = new XMLHttpRequest();
    xhr.open('GET', "/measure", true);
    xhr.send();
  }
  function rotatePhoto() {
    var img = document.getElementById("photo");
    deg += 90;
    if(isOdd(deg/90)){ document.getElementById("container").className = "vert"; }
    else{ document.getElementById("container").className = "hori"; }
    img.style.transform = "rotate(" + deg + "deg)";
  }
  function isOdd(n) { return Math.abs(n % 2) == 1; }
</script>
</html>)rawliteral";

void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200);

  // Connect to Wi-Fi
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("ESP");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    ESP.restart();
  }
  else {
    delay(500);
    Serial.println("SPIFFS mounted successfully");
  }

  // Print ESP32 Local IP Address
  Serial.print("IP Address: http://");
  Serial.println(WiFi.localIP());

  // Turn-off the 'brownout detector'
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // OV2640 camera module
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 6;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 6;
    config.fb_count = 1;
  }
  
  // Camera init
  
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    ESP.restart();
  }

  // Route for root / web pageRECIPIENT_EMAIL@gmail.com
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/html", index_html);
  });

  server.on("/capture", HTTP_GET, [](AsyncWebServerRequest * request) {
    takeNewPhoto = true;
    request->send_P(200, "text/plain", "Taking Photo");
  });

  server.on("/measure", HTTP_GET, [](AsyncWebServerRequest * request) {
    takeNewScaleMeasurement = true;
    request->send(SPIFFS, "/weights.txt", "text/plain");
  });

  server.on("/saved-photo", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(SPIFFS, FILE_PHOTO, "image/jpg", false);
  });
  
  server.on("/sendPhoto", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.println(" gotrequest to send");
    sendImage();
  });

  // Start server
  
  server.begin();

  // set the time

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  //getTime();

  pinMode(FLASH_GPIO_NUM, OUTPUT);
}

void loop() {
  if (takeNewPhoto) {
    capturePhotoSaveSpiffs();
    takeNewPhoto = false;
  };
  delay(1);
  if (takeNewScaleMeasurement) {
    Serial.println("got into measure in loop");
    measureScale();
    takeNewScaleMeasurement = false;
  }
  delay(1);
}

// Check if photo capture was successful
bool checkPhoto( fs::FS &fs ) {
  File f_pic = fs.open( FILE_PHOTO );
  unsigned int pic_sz = f_pic.size();
  return ( pic_sz > 100 );
}

// Capture Photo and Save it to SPIFFS
void capturePhotoSaveSpiffs( void ) {
  camera_fb_t * fb = NULL; // pointer
  bool ok = 0; // Boolean indicating if the picture has been taken correctly
  digitalWrite(FLASH_GPIO_NUM, HIGH);
  do {
    // Take a photo with the camera
    Serial.println("Taking a photo...");
    fb = esp_camera_fb_get();
    delay(0.5);
    if (!fb) {
      Serial.println("Camera capture failed");
      return;
    }
    // Photo file name
    Serial.printf("Picture file name: %s\n", FILE_PHOTO);
    File file = SPIFFS.open(FILE_PHOTO, FILE_WRITE);

    // Insert the data in the photo file
    if (!file) {
      Serial.println("Failed to open file in writing mode");
    }
    else {
      file.write(fb->buf, fb->len); // payload (image), payload length
      Serial.print("The picture has been saved in ");
      Serial.print(FILE_PHOTO);
      Serial.print(" - Size: ");
      Serial.print(file.size());
      Serial.println(" bytes");
    }
    // Close the fileweights.print("\t");
    file.close();
    esp_camera_fb_return(fb);

    // check if file has been correctly saved in SPIFFS
    ok = checkPhoto(SPIFFS);
  } while ( !ok );
  digitalWrite(FLASH_GPIO_NUM, LOW);

}


void sendImage( void ) {
  Serial.println("I am in sendImage");
  smtpData.setLogin(SMTP_Server, SMTP_Server_Port, sender_email, sender_email_password);
  smtpData.setSender("ESP32-CAM", sender_email);
  smtpData.setPriority("High");
  smtpData.setSubject(email_subject);
  smtpData.setMessage("PFA ESP32-CAM Captured Image.", false);
  smtpData.addRecipient(email_recipient);
  smtpData.addAttachFile(FILE_PHOTO, "image/jpg");
  smtpData.addAttachFile("/weights.txt");
  smtpData.setFileStorageType(MailClientStorageType::SPIFFS);
  smtpData.setSendCallback(sendCallback);
  
  if (!MailClient.sendMail(smtpData))
    Serial.println("Error sending Email, " + MailClient.smtpErrorReason());

  smtpData.empty();
}

void sendCallback(SendStatus msg) {
  Serial.println(msg.info());
}

void measureScale( void ) {
  Serial.println("in measure function");
  delay(15);
  rtc_clk_cpu_freq_set(RTC_CPU_FREQ_80M);
  Serial.println("after freq change");
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.power_up();
  scale.set_scale(-386.44);
  if (!scaleCalibrated)
    {
      scale.tare();
      scaleCalibrated = true;
    };
  Serial.println("before check file");
  if (delete_weight_file && SPIFFS.exists("/weights.txt")) {
    SPIFFS.remove("/weights.txt");
  };
  Serial.println("after check file, before open file");
  weights = SPIFFS.open("/weights.txt", "a");
  delay(2);
  Serial.println("after open file, before measuring");
  if (!weights) {
    Serial.println("could not open storage file *weights*");
  } else {
    weight = scale.get_units(5);
    //weight = 3.43;
  };
  scale.power_down();
  rtc_clk_cpu_freq_set(RTC_CPU_FREQ_240M);
  delay(15);
  if (!weight or weight == 0.0) {
    Serial.println("could not get data from scale");
  } else {
    int moment[5] = {0,0,0,0,0};
    fillTime(moment);
    //moment = getTime();
    Serial.println("before printing moment");
    for(int i = 0; i < 5; i++){ Serial.println(moment[i]);};
    Serial.println(weight, 1);        // print the average of 5 readings from the ADC minus tare weight, divided by the SCALE parameter set with set_scale
    weights.print(weight, 2);
    weights.print("\t");
    weights.print(moment[0]);
    weights.print(":");
    weights.print(moment[1]);
    weights.print(" ");
    weights.print(moment[2]);
    weights.print(".");
    weights.print(moment[3]);
    weights.print(".");
    weights.print(moment[4]);
    weights.print("\n");
  };
  Serial.println("before closing *weights*");
  weights.close();
  Serial.println("after closing *weights*");
};

void fillTime(int (& emptyarray) [5] ) {
  time_t rawtime;
  struct tm * timeinfo;
  time ( &rawtime );
  timeinfo = localtime ( &rawtime );
  emptyarray[0] = timeinfo->tm_hour; 
  emptyarray[1] = timeinfo->tm_min;
  emptyarray[2] = timeinfo->tm_mday;
  emptyarray[3] = timeinfo->tm_mon + 1;
  emptyarray[4] = timeinfo->tm_year + 1900;
};
