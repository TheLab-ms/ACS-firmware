#include "time.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wiegand.h>
#include "GlobalStructs.h"

#include "secrets.h"
#include "pins.h"

char ssid[] = SECRET_WIFI_SSID;
char password[] = SECRET_WIFI_PASS;

const char *server = SECRET_FOB_SERVER_URL; // Server URL

char *ntpServer = "pool.ntp.org";
unsigned long epochTime;

byte hexIDArray[ARRAY_SPACE];
int arrayPosition = -1;
byte hex1;
byte hex2;
int decimalID = 0;
int facilityCodeDecimal = 0;
int cardCodeDecimal = 0;
int receiveDataTriggered = 0;
int authorized;
// #define allowedIDsNumber 2
// int allowedIDsArray[allowedIDsNumber] = {8668797, 14108649};
Wiegand wiegand;   // The object that handles the wiegand protocol
void printCodes(); // function to print the ID decimal code, fecility code, card

int cardCheckFunction(int);
void unlockDoor(int, bool, String);
void pinStateChanged();
void receivedData(uint8_t *, uint8_t, const char *);
void addFobData(String, int, int);
void initWiFi();
void printFobsData();
unsigned long getTime();
void sendPostRequest(int, String, bool, unsigned long);

FobData *head = nullptr;
String authorizedUserID;

const char *test_root_ca = SECRET_ROOT_CA;

// You can use x.509 client certificates if you want

// to verify the client
const char *test_client_key = SECRET_CLIENT_KEY;

// to verify the client
const char *test_client_cert = SECRET_CLIENT_CERT;

WiFiClientSecure client;

void setup()
{
  // Initialize serial and wait for port to open:
  Serial.begin(115200);
  delay(100);

  Serial.print("Attempting to connect to SSID: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  // attempt to connect to Wifi network:
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    // wait 1 second for re-trying
    delay(1000);
  }

  Serial.print("Connected to ");
  Serial.println(ssid);

  client.setCACert(test_root_ca);
  client.setCertificate(test_client_cert); // for client verification
  client.setPrivateKey(test_client_key);   // for client verification

  Serial.println("\nStarting connection to server...");
  if (!client.connect(SECRET_FOB_SERVER_URL, 443))
    Serial.println("Connection failed!");
  else
    Serial.println("Connected to server!");
  // Make a HTTP request:
  client.println("GET https://" SECRET_FOB_SERVER_URL "/v1/fobs HTTP/1.1");
  client.println("Host: " SECRET_FOB_SERVER_URL);
  client.println("Connection: close");
  client.println();

  // Parse the JSON response
  JsonDocument doc; // Adjust the size as needed
  DeserializationError error;

  String jsonResponse = "";                // Initialize jsonResponse
  unsigned long timeout = millis() + 5000; // Timeout in 5 seconds
  while (client.connected())
  {
    String line = client.readStringUntil('\n');
    Serial.println(line);
    if (line == "\r")
    {
      Serial.println("headers received");
      break;
    }
  }
  int count = 0;
  while (client.connected() && millis() < timeout)
  {
    if (client.available())
    {
      char c = client.read();
      if (c != '\n' && count >= 4)
        jsonResponse += c;       // Append each character to jsonResponse
      timeout = millis() + 5000; // Reset timeout
      count++;
    }
  }
  jsonResponse[count - 5] = '\0';
  // Print the received JSON data
  Serial.println("Received JSON data:");
  Serial.println(jsonResponse);

  // Parse the JSON response
  error = deserializeJson(doc, jsonResponse);

  if (error)
  {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  // Access and print JSON data
  if (doc.is<JsonArray>())
  {
    JsonArray array = doc.as<JsonArray>();
    for (JsonObject obj : array)
    {
      // Store data in linked list
      addFobData(obj["userID"].as<String>(), obj["fobID"].as<int>(),
                 obj["ttl"].as<int>());
    }
  }
  else
  {
    Serial.println("Invalid JSON format: not an array.");
  }

  client.stop();
  printFobsData();
  Serial.println("Please enter a fob ID:");
  initWiFi();
  configTime(0, 0, ntpServer);
  wiegand.onReceive(receivedData, "Card readed: ");
  wiegand.begin(Wiegand::LENGTH_ANY, true);
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_D0, INPUT);
  pinMode(PIN_D1, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_D0), pinStateChanged, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_D1), pinStateChanged, CHANGE);
  pinStateChanged();
  digitalWrite(PIN_RELAY, HIGH);
}

void loop()
{
  noInterrupts();
  wiegand.flush();
  interrupts();
  if (receiveDataTriggered == 1)
  {
    // printing ID in decimal, binary, hex, facility code, card code:
    // GET TIMESTAMP HERE
    epochTime = getTime();
    printCodes();
    // check if the card ID is allowed:
    Serial.print("DecimalID before passing to cardCheckFunction: ");
    Serial.println(decimalID);
    cardCheckFunction(decimalID);
    // unlocks door if the ID is allowed:
    // unlockDoor(decimalID,authorized, authorizedUserID);
    receiveDataTriggered = 0;
  }
  // delay(100);
}

// When any of the pins have changed, update the state of the wiegand library
void pinStateChanged()
{
  wiegand.setPin0State(digitalRead(PIN_D0));
  wiegand.setPin1State(digitalRead(PIN_D1));
}

// Notifies when a card was read.
// Instead of a message, the seconds parameter can be anything you want --
// Whatever you specify on `wiegand.onReceive()`
void receivedData(uint8_t *data, uint8_t bits, const char *message)
{
  receiveDataTriggered = 1;
  Serial.print(message);
  Serial.print(bits);
  Serial.print("bits / ");
  // Print value in HEX
  uint8_t bytes = (bits + 7) / 8;
  for (int i = 0, arrayPosition = -1; i < bytes; i++)
  {
    Serial.print(data[i] >> 4, 16);
    Serial.print(data[i] & 0xF, 16);
    hex1 = data[i] >> 4, 16;
    hex2 = data[i] & 0xF, 16;
    // array of HEX digits of ID (each of the 6 digits in in decimal)
    arrayPosition = arrayPosition + 1;
    hexIDArray[arrayPosition] = hex1;
    arrayPosition++;
    hexIDArray[arrayPosition] = hex2;
  }
  // calculating decimal ID:
  decimalID = 0;
  for (int j = 0; j < ARRAY_SPACE; j++)
  {
    decimalID += hexIDArray[j] * pow(16, ARRAY_SPACE - j - 1);
  }
  // calculating facility code:
  facilityCodeDecimal = 0;
  for (int j = 0; j < 2; j++)
  {
    facilityCodeDecimal += hexIDArray[j] * pow(16, 2 - j - 1);
  }
  // calculating card code:
  cardCodeDecimal = 0;
  for (int j = 2; j < 6; j++)
  {
    cardCodeDecimal += hexIDArray[j] * pow(16, 6 - j - 1);
  }
}

void printCodes()
{
  Serial.println();
  Serial.print("The ID in decimal is: ");
  Serial.println(decimalID);
  Serial.print("The facility code in decimal is: ");
  Serial.println(facilityCodeDecimal);
  Serial.print("The card code in decimal is: ");
  Serial.println(cardCodeDecimal);
  Serial.print("The epoch time in seconds timestamp is: ");
  Serial.println(epochTime);
}

int cardCheckFunction(int decimalID)
{
  Serial.print("Decimal ID from cardCheckFunction: ");
  Serial.println(decimalID);
  FobData *current = head;
  bool authorized = false;
  while (current != nullptr)
  {
    if (current->fobID == decimalID)
    {
      authorized = true;
      authorizedUserID = current->userID;
      break;
    }
    current = current->next;
  }
  // return authorized ? 1 : 0;
  if (authorized)
  {
    // Serial.println("Authorized");
  }
  else
  {
    // Serial.println("Not authorized");
    authorizedUserID = "null";
  }
  unlockDoor(decimalID, authorized, authorizedUserID);
}

void unlockDoor(int decimalID, bool authorized, String authorizedUserID)
{
  // unsigned long time = 1714079477;
  FobData *current = head;
  // bool authorized = false;
  // authorizedUserID = "null";
  // Serial.print("DECID: ");
  // Serial.println(decimalID);

  // Serial.print("Auth: ");
  // Serial.println(authorized);
  // authorizedUserID = current->userID;

  // if the ID is allowed, opens lock:
  if (authorized)
  {
    digitalWrite(PIN_RELAY, LOW);
    Serial.println("ACCESS GRANTED");
    Serial.println();
    delay(7000);
    digitalWrite(PIN_RELAY, HIGH);
  }
  else
  {
    digitalWrite(PIN_RELAY, HIGH);
    Serial.println("ACCESS DENIED");
    Serial.println();
  }
  sendPostRequest(decimalID, authorizedUserID, authorized, epochTime);
}

unsigned long getTime()
{
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    // Serial.println("Failed to obtain time");
    return (0);
  }
  time(&now);
  return now;
}

void initWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  int tries = 0;
  while ((WiFi.status() != WL_CONNECTED) && tries <= 10)
  {
    Serial.print('.');
    delay(1000);
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("WiFi connection established");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("Cannot establish a WiFi connection");
  }
}

void printFobsData()
{
  Serial.println("Fob Data:");
  FobData *current = head;
  int i = 0;
  while (current != nullptr)
  {
    Serial.print("Fob ");
    Serial.print(i);
    Serial.print(": userID = ");
    Serial.print(current->userID);
    Serial.print(", fobID = ");
    Serial.print(current->fobID);
    Serial.print(", ttl = ");
    Serial.println(current->ttl);
    current = current->next;
    i++;
  }
}

void addFobData(String userID, int fobID, int ttl)
{
  FobData *newNode = new FobData;
  newNode->userID = userID;
  newNode->fobID = fobID;
  newNode->ttl = ttl;
  newNode->next = nullptr;

  if (head == nullptr)
  {
    head = newNode;
  }
  else
  {
    FobData *current = head;
    while (current->next != nullptr)
    {
      current = current->next;
    }
    current->next = newNode;
  }
}

void sendPostRequest(int userFobID, String authorizedUserID, bool authorized,
                     unsigned long scanTime)
{
  // Construct JSON payload
  String payload = String("{") + "\"timestamp\":" + String(scanTime) + ",\"fobID\":" + String(userFobID) + ",\"userID\":\"" + authorizedUserID + "\"" + ",\"authorized\":" + (authorized ? "true" : "false") + "}";

  // Make sure you establish a WiFi connection here if not already connected

  // Make a HTTP request:
  String url = "https://" SECRET_FOB_SERVER_URL "/v1/events"; // Endpoint for POST request

  if (client.connect(server, 443))
  {
    // Send the POST request
    client.print("POST ");
    Serial.print("POST ");
    client.print(url);
    Serial.print(url);
    client.println(" HTTP/1.1");
    Serial.println(" HTTP/1.1");
    client.print("Host: ");
    Serial.print("Host: ");
    client.println(server);
    Serial.println(server);
    client.println("Connection: close");
    Serial.println("Connection: close");
    client.println("Content-Type: application/json");
    Serial.println("Content-Type: application/json");
    client.print("Content-Length: ");
    Serial.print("Content-Length: ");
    client.println(payload.length());
    Serial.println(payload.length());
    client.println();
    client.println(payload);
    Serial.println(payload);

    Serial.print("Client connected: ");
    Serial.println(client.connected());
    Serial.print("Client available: ");
    Serial.println(client.available());
    // Print response
    Serial.println("\nResponse from server:");
    while (client.connected() || client.available())
    {
      if (client.available())
      {
        String line = client.readStringUntil('\n');
        Serial.println(line);
      }
    }
    Serial.println("\nAfter while loop.....");
    client.stop(); // Close the connection
  }
  else
  {
    Serial.println("Failed to connect to the server in POST");
  }
}
