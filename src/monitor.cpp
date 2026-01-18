#include <Arduino.h>
#include <WiFi.h>
#include "mbedtls/md.h"
#include "HTTPClient.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <list>
#include "mining.h"
#include "utils.h"
#include "monitor.h"
#include "drivers/storage/storage.h"
#include "drivers/devices/device.h"

// Async HTTP infrastructure
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

extern uint32_t templates;
extern uint32_t hashes;
extern uint32_t Mhashes;
extern uint32_t totalKHashes;
extern uint32_t elapsedKHs;
extern uint64_t upTime;

extern uint32_t shares; // increase if blockhash has 32 bits of zeroes
extern uint32_t valids; // increased if blockhash <= targethalfshares

extern double best_diff; // track best diff

extern monitor_data mMonitor;

//from saved config
extern TSettings Settings; 
bool invertColors = false;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "europe.pool.ntp.org", 3600, 60000);
unsigned int bitcoin_price=0;
String current_block = "793261";
global_data gData;
pool_data pData;
String poolAPIUrl;

// Update timers for HTTP data refresh intervals
unsigned long mGlobalUpdate = 0;
unsigned long mHeightUpdate = 0;
unsigned long mBTCUpdate = 0;
unsigned long mTriggerUpdate = 0;
unsigned long mPoolUpdate = 0;
unsigned long initialMillis = millis();
unsigned long initialTime = 0;

// ===== Async HTTP Client Infrastructure =====

// Request types for async HTTP fetcher
enum HttpRequestType {
    HTTP_REQ_GLOBAL_HASHRATE,  // Global hashrate and difficulty
    HTTP_REQ_FEES,              // Network fees
    HTTP_REQ_BLOCK_HEIGHT,
    HTTP_REQ_BTC_PRICE,
    HTTP_REQ_POOL_DATA
};

// HTTP request structure
// NOTE: Using char array instead of String for FreeRTOS queue compatibility
// String objects contain pointers that become invalid after queueing
#define HTTP_URL_MAX_LEN 512

struct HttpRequest {
    HttpRequestType type;
    char url[HTTP_URL_MAX_LEN];  // Fixed-size buffer for URL (512 bytes for long pool URLs)
    unsigned long timestamp;     // Timestamp when request was queued (reserved for future use)
};

// FreeRTOS task and sync primitives
TaskHandle_t httpFetcherTask = NULL;
QueueHandle_t httpRequestQueue = NULL;
SemaphoreHandle_t httpDataMutex = NULL;

#define HTTP_QUEUE_SIZE 10
#define HTTP_TASK_STACK_SIZE 16384  // 16KB required for SSL/TLS operations
#define HTTP_TASK_PRIORITY 2  // Lower than monitor task (5) but higher than miner (1)

// ===== HTTP Response Processors =====

void processGlobalDataResponse(const String& payload) {
    // Check if mutex was initialized (Issue #6: NULL check)
    if (httpDataMutex == NULL) {
        Serial.println("ERROR: HTTP data mutex not initialized");
        return;
    }
    
    // Use DynamicJsonDocument (heap) instead of stack to reduce stack pressure (Issue #8)
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        Serial.println("Global data JSON parse error");
        return;
    }
    
    xSemaphoreTake(httpDataMutex, portMAX_DELAY);
    
    // Use double for better precision with large numbers, avoid fragile string manipulation
    if (doc.containsKey("currentHashrate")) {
        double hashrate = doc["currentHashrate"].as<double>();
        // An exahash is 1e18
        long exahashes = round(hashrate / 1.0e18);
        gData.globalHash = String(exahashes);
    }
    
    if (doc.containsKey("currentDifficulty")) {
        double difficulty = doc["currentDifficulty"].as<double>();
        // A terahash is 1e12
        double terahashes = difficulty / 1.0e12;
        char difficultyStr[16];
        snprintf(difficultyStr, sizeof(difficultyStr), "%.2fT", terahashes);
        gData.difficulty = String(difficultyStr);
    }
    
    xSemaphoreGive(httpDataMutex);
    doc.clear();
}

void processFeesResponse(const String& payload) {
    // Check if mutex was initialized (Issue #6: NULL check)
    if (httpDataMutex == NULL) {
        Serial.println("ERROR: HTTP data mutex not initialized");
        return;
    }
    
    // Use DynamicJsonDocument (heap) instead of stack to reduce stack pressure (Issue #8)
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        Serial.println("Fees JSON parse error");
        return;
    }
    
    xSemaphoreTake(httpDataMutex, portMAX_DELAY);
    
    if (doc.containsKey("halfHourFee")) gData.halfHourFee = doc["halfHourFee"].as<int>();
#ifdef SCREEN_FEES_ENABLE
    if (doc.containsKey("fastestFee"))  gData.fastestFee = doc["fastestFee"].as<int>();
    if (doc.containsKey("hourFee"))     gData.hourFee = doc["hourFee"].as<int>();
    if (doc.containsKey("economyFee"))  gData.economyFee = doc["economyFee"].as<int>();
    if (doc.containsKey("minimumFee"))  gData.minimumFee = doc["minimumFee"].as<int>();
#endif
    
    xSemaphoreGive(httpDataMutex);
    doc.clear();
}

void processBlockHeightResponse(const String& payload) {
    // Check if mutex was initialized (Issue #6: NULL check)
    if (httpDataMutex == NULL) {
        Serial.println("ERROR: HTTP data mutex not initialized");
        return;
    }
    
    String trimmed = payload;
    trimmed.trim();
    
    xSemaphoreTake(httpDataMutex, portMAX_DELAY);
    current_block = trimmed;
    xSemaphoreGive(httpDataMutex);
}

void processBTCPriceResponse(const String& payload) {
    // Check if mutex was initialized (Issue #6: NULL check)
    if (httpDataMutex == NULL) {
        Serial.println("ERROR: HTTP data mutex not initialized");
        return;
    }
    
    // Use DynamicJsonDocument (heap) instead of stack to reduce stack pressure (Issue #8)
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, payload);
    if (error) {
        Serial.println("BTC price JSON parse error");
        return;
    }
    
    xSemaphoreTake(httpDataMutex, portMAX_DELAY);
    
    if (doc.containsKey("bitcoin") && doc["bitcoin"].containsKey("usd")) {
        bitcoin_price = doc["bitcoin"]["usd"];
    }
    
    xSemaphoreGive(httpDataMutex);
    doc.clear();
}

void processPoolDataResponse(const String& payload) {
    // Check if mutex was initialized (Issue #6: NULL check)
    if (httpDataMutex == NULL) {
        Serial.println("ERROR: HTTP data mutex not initialized");
        return;
    }
    
    StaticJsonDocument<300> filter;
    filter["bestDifficulty"] = true;
    filter["workersCount"] = true;
    filter["workers"][0]["sessionId"] = true;
    filter["workers"][0]["hashRate"] = true;
    
    // Use DynamicJsonDocument (heap) for large docs to reduce stack pressure (Issue #8)
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
    if (error) {
        Serial.println("Pool data JSON parse error: " + String(error.c_str()));
        Serial.println("Payload length: " + String(payload.length()));
        Serial.println("First 200 chars: " + payload.substring(0, min(200, (int)payload.length())));
        
        // Set error state
        xSemaphoreTake(httpDataMutex, portMAX_DELAY);
        pData.bestDifficulty = "Parse Error";
        pData.workersHash = "E";
        pData.workersCount = 0;
        xSemaphoreGive(httpDataMutex);
        return;
    }
    
    xSemaphoreTake(httpDataMutex, portMAX_DELAY);
    
    if (doc.containsKey("workersCount")) {
        pData.workersCount = doc["workersCount"].as<int>();
        Serial.println("Workers count: " + String(pData.workersCount));
    } else {
        Serial.println("WARNING: workersCount not in response");
    }
    
    const JsonArray& workers = doc["workers"].as<JsonArray>();
    float totalhashs = 0;
    for (const JsonObject& worker : workers) {
        totalhashs += worker["hashRate"].as<double>();
    }
    
    char totalhashs_s[16] = {0};
    suffix_string(totalhashs, totalhashs_s, 16, 0);
    pData.workersHash = String(totalhashs_s);
    Serial.println("Workers hash: " + pData.workersHash);
    
    if (doc.containsKey("bestDifficulty")) {
        double temp = doc["bestDifficulty"].as<double>();
        char best_diff_string[16] = {0};
        suffix_string(temp, best_diff_string, 16, 0);
        pData.bestDifficulty = String(best_diff_string);
        Serial.println("Best difficulty: " + pData.bestDifficulty);
    } else {
        Serial.println("WARNING: bestDifficulty not in response");
    }
    
    xSemaphoreGive(httpDataMutex);
    doc.clear();
    
    Serial.println("####### Pool Data processed (async)");
}

// ===== Async HTTP Fetcher Task =====

void httpFetcherTaskHandler(void* param) {
    HTTPClient http;
    HttpRequest req;
    
    Serial.println("HTTP Fetcher Task started");
    
    // Monitor initial stack usage (Issue #7: Stack monitoring)
    UBaseType_t highWaterMark = uxTaskGetStackHighWaterMark(NULL);
    Serial.printf("HTTP task initial stack high water mark: %u words (%u bytes free)\n", 
                  highWaterMark, highWaterMark * 4);
    
    int requestCount = 0;
    
    while (1) {
        // Wait for HTTP request from queue (blocking)
        if (xQueueReceive(httpRequestQueue, &req, portMAX_DELAY) == pdTRUE) {
            
            requestCount++;
            Serial.println("Processing HTTP request type: " + String(req.type) + " URL: " + String(req.url));
            
            // Check WiFi connectivity
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("HTTP request skipped: No WiFi");
                continue;
            }
            
            // Perform HTTP request
            http.setTimeout(10000);
            http.begin(req.url);
            
            int httpCode = http.GET();
            
            Serial.println("HTTP response code: " + String(httpCode));
            
            if (httpCode == HTTP_CODE_OK) {
                // Heap monitoring before payload allocation (Issue #9)
                size_t freeHeapBefore = ESP.getFreeHeap();
                int contentLength = http.getSize(); // -1 if unknown
                
                Serial.printf("Free heap before payload: %u bytes, content length: %d\n", 
                              freeHeapBefore, contentLength);
                
                // Check if enough heap available (safety margin: 20KB)
                if (contentLength > 0 && freeHeapBefore < (size_t)contentLength + 20000) {
                    Serial.println("ERROR: Insufficient heap for HTTP payload");
                    http.end();
                    continue;
                }
                
                String payload;
                // Reserve space if size is known (optimization)
                if (contentLength > 0 && contentLength < 128 * 1024) {
                    payload.reserve(contentLength + 1);
                }
                
                payload = http.getString();
                
                // Heap monitoring after payload allocation (Issue #9)
                size_t freeHeapAfter = ESP.getFreeHeap();
                Serial.printf("Payload received, length: %u, heap after: %u (delta: %d bytes)\n", 
                              payload.length(), freeHeapAfter, 
                              (int)(freeHeapBefore - freeHeapAfter));
                
                // Check for allocation failure
                if (contentLength > 0 && payload.length() == 0) {
                    Serial.println("ERROR: HTTP payload allocation failed");
                    http.end();
                    continue;
                }
                
                Serial.println("Payload received, length: " + String(payload.length()));
                
                // Process response based on request type
                switch (req.type) {
                    case HTTP_REQ_GLOBAL_HASHRATE:
                        processGlobalDataResponse(payload);
                        break;
                        
                    case HTTP_REQ_FEES:
                        processFeesResponse(payload);
                        break;
                        
                    case HTTP_REQ_BLOCK_HEIGHT:
                        processBlockHeightResponse(payload);
                        break;
                        
                    case HTTP_REQ_BTC_PRICE:
                        processBTCPriceResponse(payload);
                        break;
                        
                    case HTTP_REQ_POOL_DATA:
                        Serial.println("Processing pool data...");
                        processPoolDataResponse(payload);
                        break;
                }
                
            } else {
                Serial.printf("HTTP request failed: %d (type: %d)\n", httpCode, req.type);
                
                // Handle pool data errors specifically
                if (req.type == HTTP_REQ_POOL_DATA && httpDataMutex != NULL) {
                    xSemaphoreTake(httpDataMutex, portMAX_DELAY);
                    pData.bestDifficulty = "HTTP Err";
                    pData.workersHash = "E";
                    pData.workersCount = 0;
                    xSemaphoreGive(httpDataMutex);
                }
            }
            
            http.end();
            
            // Monitor stack usage every 10 requests (Issue #7: Stack monitoring)
            if (requestCount % 10 == 0) {
                highWaterMark = uxTaskGetStackHighWaterMark(NULL);
                Serial.printf("HTTP task stack high water mark: %u words (%u bytes free)\n", 
                              highWaterMark, highWaterMark * 4);
            }
            
            // Small delay to prevent overwhelming network
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }
}

// ===== Helper function to queue HTTP requests =====

bool queueHttpRequest(HttpRequestType type, const String& url) {
    // Check if queue was initialized (Issue #6: NULL check)
    if (httpRequestQueue == NULL) {
        Serial.println("ERROR: HTTP queue not initialized");
        return false;
    }
    
    // Validate URL length (Issue #5: Buffer overflow protection)
    if (url.length() >= HTTP_URL_MAX_LEN) {
        Serial.printf("ERROR: URL too long (%d bytes, max %d): %s\n", 
                      url.length(), HTTP_URL_MAX_LEN - 1, url.c_str());
        return false;
    }
    
    HttpRequest req;
    req.type = type;
    
    // Copy URL to fixed-size buffer (safe for FreeRTOS queue)
    strncpy(req.url, url.c_str(), sizeof(req.url) - 1);
    req.url[sizeof(req.url) - 1] = '\0';  // Ensure null termination
    
    req.timestamp = millis();
    
    // Try to send to queue (non-blocking with 0 timeout)
    if (xQueueSend(httpRequestQueue, &req, 0) == pdTRUE) {
        return true;
    } else {
        Serial.println("HTTP queue full, request dropped");
        return false;
    }
}


void setup_monitor(void){
    /******** TIME ZONE SETTING *****/

    timeClient.begin();
    
    // Adjust offset depending on your zone
    // GMT +2 in seconds (zona horaria de Europa Central)
    timeClient.setTimeOffset(3600 * Settings.Timezone);

    Serial.println("TimeClient setup done");
#ifdef SCREEN_WORKERS_ENABLE
    poolAPIUrl = getPoolAPIUrl();
    Serial.println("poolAPIUrl: " + poolAPIUrl);
#endif

    // ===== Initialize Async HTTP Infrastructure =====
    
    // Create mutex for thread-safe data access
    httpDataMutex = xSemaphoreCreateMutex();
    if (httpDataMutex == NULL) {
        Serial.println("ERROR: Failed to create HTTP data mutex");
        return;
    }
    
    // Create queue for HTTP requests
    httpRequestQueue = xQueueCreate(HTTP_QUEUE_SIZE, sizeof(HttpRequest));
    if (httpRequestQueue == NULL) {
        Serial.println("ERROR: Failed to create HTTP request queue");
        return;
    }
    
    // Create HTTP fetcher task on Core 1 (same as monitor)
    BaseType_t taskCreated = xTaskCreatePinnedToCore(
        httpFetcherTaskHandler,     // Task function
        "HttpFetcher",               // Task name
        HTTP_TASK_STACK_SIZE,        // Stack size (16KB)
        NULL,                        // Parameters
        HTTP_TASK_PRIORITY,          // Priority (2)
        &httpFetcherTask,            // Task handle
        1                            // Core 1 (same as monitor task)
    );
    
    if (taskCreated != pdPASS) {
        Serial.println("ERROR: Failed to create HTTP fetcher task");
        return;
    }
    
    Serial.println("Async HTTP infrastructure initialized successfully");
}


void updateGlobalData(void){
    
    if((mGlobalUpdate == 0) || (millis() - mGlobalUpdate > UPDATE_Global_min * 60 * 1000)){
    
        if (WiFi.status() != WL_CONNECTED) return;
        
        // Queue async HTTP requests for global data
        bool queued1 = queueHttpRequest(HTTP_REQ_GLOBAL_HASHRATE, getGlobalHash);
        bool queued2 = queueHttpRequest(HTTP_REQ_FEES, getFees);
        
        // Update timer if at least one request was successfully queued
        // This prevents retry storm when queue is full
        if (queued1 || queued2) {
            mGlobalUpdate = millis();
        }
    }
}


String getBlockHeight(void){
    
    if((mHeightUpdate == 0) || (millis() - mHeightUpdate > UPDATE_Height_min * 60 * 1000)){
    
        if (WiFi.status() != WL_CONNECTED) {
            // Return cached value with mutex protection (Issue #1: Race condition fix)
            xSemaphoreTake(httpDataMutex, portMAX_DELAY);
            String result = current_block;
            xSemaphoreGive(httpDataMutex);
            return result;
        }
        
        // Queue async HTTP request for block height
        bool queued = queueHttpRequest(HTTP_REQ_BLOCK_HEIGHT, getHeightAPI);
        
        // Update timer if request was successfully queued to prevent retry storm
        if (queued) {
            mHeightUpdate = millis();
        }
    }
  
    // Return current cached value with mutex protection (Issue #1: Race condition fix)
    xSemaphoreTake(httpDataMutex, portMAX_DELAY);
    String result = current_block;
    xSemaphoreGive(httpDataMutex);
    return result;
}


String getBTCprice(void){
    
    if((mBTCUpdate == 0) || (millis() - mBTCUpdate > UPDATE_BTC_min * 60 * 1000)){
    
        if (WiFi.status() != WL_CONNECTED) {
            // Return cached value with mutex protection (Issue #1: Race condition fix)
            xSemaphoreTake(httpDataMutex, portMAX_DELAY);
            static char price_buffer[16];
            snprintf(price_buffer, sizeof(price_buffer), "$%u", bitcoin_price);
            xSemaphoreGive(httpDataMutex);
            return String(price_buffer);
        }
        
        // Queue async HTTP request for BTC price
        bool queued = queueHttpRequest(HTTP_REQ_BTC_PRICE, getBTCAPI);
        
        // Update timer if request was successfully queued to prevent retry storm
        if (queued) {
            mBTCUpdate = millis();
        }
    }  
  
    // Return current cached value with mutex protection (Issue #1: Race condition fix)
    xSemaphoreTake(httpDataMutex, portMAX_DELAY);
    static char price_buffer[16];
    snprintf(price_buffer, sizeof(price_buffer), "$%u", bitcoin_price);
    xSemaphoreGive(httpDataMutex);
    return String(price_buffer);
}


void getTime(unsigned long* currentHours, unsigned long* currentMinutes, unsigned long* currentSeconds){
  
  //Check if need an NTP call to check current time
  if((mTriggerUpdate == 0) || (millis() - mTriggerUpdate > UPDATE_PERIOD_h * 60 * 60 * 1000)){ //60 sec. * 60 min * 1000ms
    if(WiFi.status() == WL_CONNECTED) {
        if(timeClient.update()) mTriggerUpdate = millis(); //NTP call to get current time
        initialTime = timeClient.getEpochTime(); // Guarda la hora inicial (en segundos desde 1970)
        Serial.print("TimeClient NTPupdateTime ");
    }
  }

  unsigned long elapsedTime = (millis() - mTriggerUpdate) / 1000; // Tiempo transcurrido en segundos
  unsigned long currentTime = initialTime + elapsedTime; // La hora actual

  // convierte la hora actual en horas, minutos y segundos
  *currentHours = currentTime % 86400 / 3600;
  *currentMinutes = currentTime % 3600 / 60;
  *currentSeconds = currentTime % 60;
}

String getDate(){
  
  unsigned long elapsedTime = (millis() - mTriggerUpdate) / 1000; // Tiempo transcurrido en segundos
  unsigned long currentTime = initialTime + elapsedTime; // La hora actual

  // Convierte la hora actual (epoch time) en una estructura tm
  struct tm *tm = localtime((time_t *)&currentTime);

  int year = tm->tm_year + 1900; // tm_year es el número de años desde 1900
  int month = tm->tm_mon + 1;    // tm_mon es el mes del año desde 0 (enero) hasta 11 (diciembre)
  int day = tm->tm_mday;         // tm_mday es el día del mes

  char currentDate[20];
  sprintf(currentDate, "%02d/%02d/%04d", tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900);

  return String(currentDate);
}

String getTime(void){
  unsigned long currentHours, currentMinutes, currentSeconds;
  getTime(&currentHours, &currentMinutes, &currentSeconds);

  char LocalHour[10];
  sprintf(LocalHour, "%02d:%02d", currentHours, currentMinutes);
  
  String mystring(LocalHour);
  return LocalHour;
}

enum EHashRateScale
{
  HashRateScale_99KH,
  HashRateScale_999KH,
  HashRateScale_9MH
};

static EHashRateScale s_hashrate_scale = HashRateScale_99KH;
static uint32_t s_skip_first = 3;
static double s_top_hashrate = 0.0;

static std::list<double> s_hashrate_avg_list;
static double s_hashrate_summ = 0.0;
static uint8_t s_hashrate_recalc = 0;

String getCurrentHashRate(unsigned long mElapsed)
{
  double hashrate = (double)elapsedKHs * 1000.0 / (double)mElapsed;

  s_hashrate_summ += hashrate;
  s_hashrate_avg_list.push_back(hashrate);
  if (s_hashrate_avg_list.size() > 10)
  {
    s_hashrate_summ -= s_hashrate_avg_list.front();
    s_hashrate_avg_list.pop_front();
  }

  ++s_hashrate_recalc;
  if (s_hashrate_recalc == 0)
  {
    s_hashrate_summ = 0.0;
    for (auto itt = s_hashrate_avg_list.begin(); itt != s_hashrate_avg_list.end(); ++itt)
      s_hashrate_summ += *itt;
  }

  double avg_hashrate = s_hashrate_summ / (double)s_hashrate_avg_list.size();
  if (avg_hashrate < 0.0)
    avg_hashrate = 0.0;

  if (s_skip_first > 0)
  {
    s_skip_first--;
  } else
  {
    if (avg_hashrate > s_top_hashrate)
    {
      s_top_hashrate = avg_hashrate;
      if (avg_hashrate > 999.9)
        s_hashrate_scale = HashRateScale_9MH;
      else if (avg_hashrate > 99.9)
        s_hashrate_scale = HashRateScale_999KH;
    }
  }

  switch (s_hashrate_scale)
  {
    case HashRateScale_99KH:
      return String(avg_hashrate, 2);
    case HashRateScale_999KH:
      return String(avg_hashrate, 1);
    default:
      return String((int)avg_hashrate );
  }
}

mining_data getMiningData(unsigned long mElapsed)
{
  mining_data data;

  char best_diff_string[16] = {0};
  suffix_string(best_diff, best_diff_string, 16, 0);

  char timeMining[15] = {0};
  uint64_t tm = upTime;
  int secs = tm % 60;
  tm /= 60;
  int mins = tm % 60;
  tm /= 60;
  int hours = tm % 24;
  int days = tm / 24;
  sprintf(timeMining, "%01d  %02d:%02d:%02d", days, hours, mins, secs);

  data.completedShares = shares;
  data.totalMHashes = Mhashes;
  data.totalKHashes = totalKHashes;
  data.currentHashRate = getCurrentHashRate(mElapsed);
  data.templates = templates;
  data.bestDiff = best_diff_string;
  data.timeMining = timeMining;
  data.valids = valids;
  data.temp = String(temperatureRead(), 0);
  data.currentTime = getTime();

  return data;
}

clock_data getClockData(unsigned long mElapsed)
{
  clock_data data;

  data.completedShares = shares;
  data.totalKHashes = totalKHashes;
  data.currentHashRate = getCurrentHashRate(mElapsed);
  data.btcPrice = getBTCprice();
  data.blockHeight = getBlockHeight();
  data.currentTime = getTime();
  data.currentDate = getDate();

  return data;
}

clock_data_t getClockData_t(unsigned long mElapsed)
{
  clock_data_t data;

  data.valids = valids;
  data.currentHashRate = getCurrentHashRate(mElapsed);
  getTime(&data.currentHours, &data.currentMinutes, &data.currentSeconds);

  return data;
}

coin_data getCoinData(unsigned long mElapsed)
{
  coin_data data;

  updateGlobalData(); // Update gData vars asking mempool APIs

  data.completedShares = shares;
  data.totalKHashes = totalKHashes;
  data.currentHashRate = getCurrentHashRate(mElapsed);
  data.btcPrice = getBTCprice();
  data.currentTime = getTime();
#ifdef SCREEN_FEES_ENABLE
  data.hourFee = String(gData.hourFee);
  data.fastestFee = String(gData.fastestFee);
  data.economyFee = String(gData.economyFee);
  data.minimumFee = String(gData.minimumFee);
#endif
  data.halfHourFee = String(gData.halfHourFee) + " sat/vB";
  data.netwrokDifficulty = gData.difficulty;
  data.globalHashRate = gData.globalHash;
  data.blockHeight = getBlockHeight();

  unsigned long currentBlock = data.blockHeight.toInt();
  unsigned long remainingBlocks = (((currentBlock / HALVING_BLOCKS) + 1) * HALVING_BLOCKS) - currentBlock;
  data.progressPercent = (HALVING_BLOCKS - remainingBlocks) * 100 / HALVING_BLOCKS;
  data.remainingBlocks = String(remainingBlocks) + " BLOCKS";

  return data;
}

String getPoolAPIUrl(void) {
    poolAPIUrl = String(getPublicPool);
    if (Settings.PoolAddress == "public-pool.io") {
        poolAPIUrl = "https://public-pool.io:40557/api/client/";
    } 
    else {
        if (Settings.PoolAddress == "pool.nerdminers.org") {
            poolAPIUrl = "https://pool.nerdminers.org/users/";
        }
        else {
            switch (Settings.PoolPort) {
                case 3333:
                    if (Settings.PoolAddress == "pool.sethforprivacy.com")
                        poolAPIUrl = "https://pool.sethforprivacy.com/api/client/";
                    if (Settings.PoolAddress == "pool.solomining.de")
                        poolAPIUrl = "https://pool.solomining.de/api/client/";
                    // Add more cases for other addresses with port 3333 if needed
                    break;
                case 2018:
                    // Local instance of public-pool.io on Umbrel or Start9
                    poolAPIUrl = "http://" + Settings.PoolAddress + ":2019/api/client/";
                    break;
                default:
                    poolAPIUrl = String(getPublicPool);
                    break;
            }
        }
    }
    return poolAPIUrl;
}

pool_data getPoolData(void){
    //pool_data pData;    
    if((mPoolUpdate == 0) || (millis() - mPoolUpdate > UPDATE_POOL_min * 60 * 1000)){      
        if (WiFi.status() != WL_CONNECTED) {
            // Return cached value with mutex protection (Issue #1: Race condition fix)
            xSemaphoreTake(httpDataMutex, portMAX_DELAY);
            pool_data result = pData;
            xSemaphoreGive(httpDataMutex);
            return result;
        }
        
        // Construct pool API URL with wallet address
        String btcWallet = Settings.BtcWallet;
        if (btcWallet.indexOf(".")>0) btcWallet = btcWallet.substring(0,btcWallet.indexOf("."));
        
        String poolUrl;
#ifdef SCREEN_WORKERS_ENABLE
        poolUrl = poolAPIUrl + btcWallet;
        Serial.println("Pool API : " + poolUrl);
#else
        poolUrl = String(getPublicPool) + btcWallet;
        Serial.println("Pool API (default): " + poolUrl);
#endif
        
        // Queue async HTTP request for pool data
        Serial.println("Queueing pool data request...");
        bool queued = queueHttpRequest(HTTP_REQ_POOL_DATA, poolUrl);
        Serial.println("Pool data request queued: " + String(queued ? "YES" : "NO"));
        
        // Update timer if request was successfully queued to prevent retry storm
        if (queued) {
            mPoolUpdate = millis();
        }
    }
    
    // Return current cached value with mutex protection (Issue #1: Race condition fix)
    xSemaphoreTake(httpDataMutex, portMAX_DELAY);
    pool_data result = pData;
    xSemaphoreGive(httpDataMutex);
    return result;
}
