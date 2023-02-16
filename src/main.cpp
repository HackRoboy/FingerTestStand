#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <esp32-hal-dac.h>
// MicroSD Libraries
#include "FS.h"
#include "SD_MMC.h"
// EEPROM Library
#include "EEPROM.h"
// Use 1 byte of EEPROM space
#define EEPROM_SIZE 1
// Counter for picture number
unsigned int pictureCount = 0;
bool EEPROM_Enable = false; // EEPROM  write times is limited, the default is false
#include <Servo.h>

// Note that these GPIOs are also used for the sd card
#define SERVO_ENABLE 4 // Connect mos to control servo power supply

#define NUMBER_OF_SERVO 3
static const int servosPins[NUMBER_OF_SERVO] = {15, 14, 2};
Servo servos[NUMBER_OF_SERVO];

int Amax = 180;
int Amin = 0;

int fingerTestCount = 0;
int fotoInterval = 100;  // Take a picture after fingerTest times
int timeInterval = 1000; // test interval

//
// WARNING!!! PSRAM IC required for  resolution and high JPEG quality
//            Ensure ESP32 Wrover Module or other board with PSRAM is selected
//            Partial images will be transmitted if image exceeds buffer size
//

// Select camera model
// #define CAMERA_MODEL_WROVER_KIT // Has PSRAM
// #define CAMERA_MODEL_ESP_EYE // Has PSRAM
// #define CAMERA_MODEL_M5STACK_PSRAM // Has PSRAM
// #define CAMERA_MODEL_M5STACK_V2_PSRAM // M5Camera version B Has PSRAM
// #define CAMERA_MODEL_M5STACK_WIDE // Has PSRAM
// #define CAMERA_MODEL_M5STACK_ESP32CAM // No PSRAM
// #define CAMERA_MODEL_M5STACK_UNITCAM // No PSRAM
#define CAMERA_MODEL_AI_THINKER // Has PSRAM
// #define CAMERA_MODEL_TTGO_T_JOURNAL // No PSRAM

#include "camera_pins.h"
#define DAC 26

const char *ssid = "*************";
const char *password = "*************";
const char *LOCAL_HOST = "***************";

const int LOCAL_PORT = 6667;
const int waitingTime = 10;

TaskHandle_t Task_0;
TaskHandle_t Task_1;

void startCameraServer();
WiFiClient client;

String sendPhoto(String serverName, int serverPort = 80, String serverPath = "/")
{
  String getAll;
  String getBody;

  camera_fb_t *fb = NULL;
  fb = esp_camera_fb_get();
  if (!fb)
  {
    Serial.println("Camera capture failed");
    delay(1000);
    ESP.restart();
  }
  esp_camera_fb_get();

  Serial.println("Connecting to server: " + serverName);

  if (client.connect(serverName.c_str(), serverPort))
  {
    Serial.println("Connection successful!");
    /* Content-Disposition:
     * form-data
     * name=\"imageFile\"
     * filename=\"esp32-cam.jpg
     * Content-Type: image/jpeg
     */

    uint32_t imageLen = fb->len;
    client.println("POST " + serverPath + " HTTP/1.1");
    client.println("Host: " + serverName);
    client.println("Content-Length: " + String(imageLen) + "");
    client.println("Content-Type: multipart/form-data");
    client.println("Wating-Time: " + String(waitingTime) + "");
    client.println();

    // capture
    uint8_t *fbBuf = fb->buf;
    size_t fbLen = fb->len;
    for (size_t n = 0; n < fbLen; n = n + 1024)
    {
      if (n + 1024 < fbLen)
      {
        client.write(fbBuf, 1024);
        fbBuf += 1024;
      }
      else if (fbLen % 1024 > 0)
      {
        size_t remainder = fbLen % 1024;
        client.write(fbBuf, remainder);
      }
    }

    esp_camera_fb_return(fb);

    int timoutTimer = 10000;
    long startTimer = millis();
    boolean state = false;

    while ((startTimer + timoutTimer) > millis())
    {
      Serial.print(".");
      delay(100);
      while (client.available())
      {
        char c = client.read();
        if (c == '\n')
        {
          if (getAll.length() == 0)
          {
            state = true;
          }
          getAll = "";
        }
        else if (c != '\r')
        {
          getAll += String(c);
        }
        if (state == true)
        {
          getBody += String(c);
        }
        startTimer = millis();
      }
      if (getBody.length() > 0)
      {
        break;
      }
    }
    Serial.println();
    client.stop();
    Serial.println(getBody);
  }
  else
  {
    getBody = "Connection to " + serverName + " failed.";
    Serial.println(getBody);
  }
  return getBody;
}

void initMicroSDCard()
{
  // Start the MicroSD card

  Serial.println("Mounting MicroSD Card");
  if (!SD_MMC.begin())
  {
    Serial.println("MicroSD Card Mount Failed");
    return;
  }
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE)
  {
    Serial.println("No MicroSD Card found");
    return;
  }
}
void initCamera()
{

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

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (psramFound())
  {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  }
  else
  {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID)
  {
    s->set_vflip(s, 1);       // flip it back
    s->set_brightness(s, 1);  // up the brightness just a bit
    s->set_saturation(s, -2); // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  s->set_framesize(s, FRAMESIZE_QVGA);

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif
}
void initWifi()
{
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
}
void initServo()
{

  Serial.println("Initializing Servo");
  for (int i = 0; i < NUMBER_OF_SERVO; ++i)
  {
    if (!servos[i].attach(servosPins[i], Servo::CHANNEL_NOT_ATTACHED, 0, 180, 500, 2400))
    {
      Serial.print("Servo ");
      Serial.print(i);
      Serial.println("attach error");
    }
  }
}

void useServo()
{
  digitalWrite(SERVO_ENABLE, LOW);
  SD_MMC.end();
}
void useSd()
{
  digitalWrite(SERVO_ENABLE, HIGH);
  // initMicroSDCard();
}

void takeNewPhoto(String path)
{
  useSd();
  // Take Picture with Camera

  // Setup frame buffer
  camera_fb_t *fb = esp_camera_fb_get();

  if (!fb)
  {
    Serial.println("Camera capture failed");
    return;
  }

  // Save picture to microSD card
  fs::FS &fs = SD_MMC;
  File file = fs.open(path.c_str(), FILE_WRITE);
  if (!file)
  {
    Serial.println("Failed to open file in write mode");
  }
  else
  {
    file.write(fb->buf, fb->len); // payload (image), payload length
    Serial.printf("Saved file to path: %s\n", path.c_str());
  }
  // Close the file
  file.close();

  // Return the frame buffer back to the driver for reuse
  esp_camera_fb_return(fb);
}
void takeNewPhoto()
{
  // pictureCount = EEPROM.read(0) + 1;
  // Path where new picture will be saved in SD Card
  String path = "/image" + String(fingerTestCount) + ".jpg";
  Serial.printf("Picture file name: %s\n", path.c_str());

  // Take and Save Photo
  takeNewPhoto(path);

  // Update EEPROM picture number counter
  // EEPROM.write(0, pictureCount);
  // EEPROM.commit();
}

// void moveServoTo(Servo * servo, int angle){
//   for(int i=Amin;i<Amax;i++){
//     servo.write(i);
//     sleep(10);
//   }
// }
void testFinger(int finger)
{
  for (int posDegrees = Amin; posDegrees <= Amax; posDegrees++)
  {
    servos[finger].write(posDegrees);
    // Serial.println(posDegrees);
    vTaskDelay(waitingTime);
  }

  for (int posDegrees = Amax; posDegrees >= Amin; posDegrees--)
  {
    servos[finger].write(posDegrees);
    // Serial.println(posDegrees);
    vTaskDelay(waitingTime);
  }
}
void testALLFinger()
{

  for (int posDegrees = Amin; posDegrees <= Amax; posDegrees++)
  {
    for (int i = 0; i < NUMBER_OF_SERVO; i++)
    {
      servos[i].write(posDegrees);
    }

    // Serial.println(posDegrees);
    vTaskDelay(waitingTime);
  }

  for (int posDegrees = Amax; posDegrees >= Amin; posDegrees--)
  {
    for (int i = 0; i < NUMBER_OF_SERVO; i++)
    {
      servos[i].write(posDegrees);
    }
    // Serial.println(posDegrees);
    vTaskDelay(waitingTime);
  }
}
// void fingerTest()
// {
//   useServo();

//   for (int i = 0; i < NUMBER_OF_SERVO; ++i)
//   {
//     Serial.println("test finger: " + String(i));
//     testFinger(i);
//   }
// }
void vTasksendPhotoCyclical(void *pvParameters)
{

  for (;;)
  {
    Serial.print("vtask run on ");
    Serial.print(xPortGetCoreID());
    Serial.println();
    // Task code goes here.
    Serial.println("Tighten finger test:" + String(fingerTestCount));
    useServo();
    // fingerTest();
    // tightFinger(0);
    testALLFinger();
    fingerTestCount++;
    // vTaskDelay(waitingTime);
  }
}
void setup()
{

  Serial.begin(115200);
  Serial.print("Start");
  Serial.setDebugOutput(true);
  Serial.println();

  setCpuFrequencyMhz(240);
  Serial.println(getCpuFrequencyMhz());
  Serial.println();

  Serial.print("setup run on ");
  Serial.print(xPortGetCoreID());
  Serial.println();

  pinMode(SERVO_ENABLE, OUTPUT);
  initServo();
  // Serial.print("Initializing the MicroSD card module... ");
  // useSd();
  // initMicroSDCard();

  // Serial.print("Initializing the Camera module... ");
  initCamera();

  initWifi();

  startCameraServer();

  // Serial.print("Camera Ready! Use 'http://");
  // // capture 3 pictures to wait camera to be stable
  // esp_camera_fb_get();

  // Serial.print(WiFi.localIP());
  // Serial.println("' to connect");

  // if (EEPROM_Enable)
  // {
  //   EEPROM.begin(EEPROM_SIZE);
  // }

  // xTaskCreatePinnedToCore(&vTasksendPhotoCyclical, "vTasksendPhotoCyclical", 4096, NULL, 5, NULL, 0);
}

void loop()
{
  //   // put your main code here, to run repeatedly:

  // Serial.print("loop run on ");
  // Serial.print(xPortGetCoreID());
  // Serial.println();

  //   if(fingerTestCount %fotoInterval==0){
  //     // takeNewPhoto();
  //     //  Serial.print("takeNewPhoto");
  //   }

  //   for(int i = 0; i <fotoInterval; i++){
  //     fingerTest();
  //     sleep(timeInterval);
  //   }
  //   fingerTestCount++;
  //   // takeNewPhoto();

  //   //  sleep(1000);
  // tightFinger(servo0);

  // Serial.println("Tighten finger test:" + String(fingerTestCount));
  // useServo();
  // // fingerTest();
  // // tightFinger(0);
  // testALLFinger();
  // fingerTestCount++;
}
