// Device-ul IoT din tema 01, de data aceasta pe un ESP32-C3 Super Mini.
//
// Aceeași logică și același protocol ca device-simulator din tema 01, cu trei
// taskuri FreeRTOS în loc de trei thread-uri Java:
//
//   task periodic --(coada de alarme)--> task sporadic
//        |                                     |
//        +---- telemetry ---> MQTT <--- alarm -+
//                              |
//                          commands --> command_received() --(coada de comenzi)--> task aperiodic ---> status
//
// Gata implementate (nu trebuie modificate):
//   config.h         setările: Wi-Fi, broker, STUDENT, valorile inițiale; DEVICE_ID
//                    se citește din adresa MAC a plăcii
//   connection.h/cpp Wi-Fi, ora exactă (SNTP), clientul MQTT, mqtt_publish(), epoch_ms()
//
// Așa cum este, schița se conectează la Wi-Fi și la broker și afișează pe
// Serial comenzile primite. Taskurile le scrieți voi: TODO 0 ... TODO 3.
// Exemplele de pornire sunt în laboratorul 0 din rt-labs-esp32c3; vezi
// exemple/README.md din folderul temei.
//
// Placa: ESP32C3 Dev Module, USB CDC On Boot: Enabled. Serial Monitor: 115200 baud.

#include "config.h"
#include "connection.h"

struct TemperatureEvents
{
    double temperature;
    uint32_t timestamp;
    float threshold;
    int64_t timestampEpoch;
    TemperatureEvents(double t, uint32_t ts, float th) : temperature(t), timestamp(ts), threshold(th) {}
};

QueueHandle_t alarmEvents;
QueueHandle_t commands;

/// pt run_PeriodicTelemetryTask
uint32_t volatile PERIOD_MS = PERIOD_MS_INITIAL;
float volatile THRESHOLD = THRESHOLD_INITIAL;
SemaphoreHandle_t periodMutex = xSemaphoreCreateMutex();
SemaphoreHandle_t thresholdMutex = xSemaphoreCreateMutex();

uint32_t getPeriodMs()
{
    xSemaphoreTake(periodMutex, portMAX_DELAY);
    uint32_t period = PERIOD_MS;
    xSemaphoreGive(periodMutex);
    return period;
}
float getThreshold()
{
    xSemaphoreTake(thresholdMutex, portMAX_DELAY);
    float threshold = THRESHOLD;
    xSemaphoreGive(thresholdMutex);
    return threshold;
}

void setPeriodMs(uint32_t period)
{
    xSemaphoreTake(periodMutex, portMAX_DELAY);
    PERIOD_MS = period;
    xSemaphoreGive(periodMutex);
}
void setThreshold(float threshold)
{
    xSemaphoreTake(thresholdMutex, portMAX_DELAY);
    THRESHOLD = threshold;
    xSemaphoreGive(thresholdMutex);
}

/// pt run_SporadicAlarmTask
uint32_t lastRelease = 0;

// Temperatura cipului, în °C, de la senzorul intern al ESP32-C3.
// Este temperatura siliciului, nu a camerei: imediat după pornirea Wi-Fi-ului
// poate fi în jur de 50 °C, apoi scade în câteva minute spre 35-40 °C.
// Rezoluția este de aproximativ 1 °C. Ca să provocați alarme, puneți pragul
// cu SET_THRESHOLD cu un grad sub valoarea citită.
float read_temperature(void)
{
    return temperatureRead();
}

// TODO 1: TASK PERIODIC, cu perioada T (inițial PERIOD_MS_INITIAL).
static void run_PeriodicTelemetryTask(void *arg)
{
    TickType_t last = xTaskGetTickCount(); // the first release is now
    uint32_t period;
    for (;;)
    {

        //   Fiecare job:
        //   - citește temperatura: read_temperature()
        float t = read_temperature();

        //   - publică telemetria: mqtt_publish("telemetry", "deviceId;timestamp;temperatura")
        //     (timestamp = epoch_ms(), tipărit cu %lld; temperatura cu "%.2f")
        int64_t ts = epoch_ms();
        char telemetry[80];
        snprintf(telemetry, sizeof(telemetry), "%s;%lld;%.2f", DEVICE_ID, ts, t);
        mqtt_publish("telemetry", telemetry);

        //   - dacă temperatura depășește pragul (inițial THRESHOLD_INITIAL), pune
        //     un eveniment în coada de alarme, fără să se blocheze (timeout 0).
        //     Taskul periodic NU tratează alarma: o tratează taskul sporadic.
        float threshold = getThreshold();
        if (t > threshold)
        {
            TemperatureEvents event(t, millis(), threshold);
            event.timestampEpoch = ts;
            xQueueSend(alarmEvents, &event, 0);
        }
        //   Eliberările respectă grila r_k = r_0 + kT: folosiți așteptare absolută
        //   (xTaskDelayUntil), nu relativă (delay, vTaskDelay), ca să nu apară derivă.
        //   Perioada și pragul se schimbă prin comenzi (taskul aperiodic), deci sunt
        //   variabile globale volatile, citite la fiecare job.
        period = getPeriodMs();
        xTaskDelayUntil(&last, pdMS_TO_TICKS(period));
    }
}

// TODO 2: TASK SPORADIC, tratează alarmele de temperatură.
void run_SporadicAlarmTask(void *arg)
{
    for (;;)
    {
        //   - așteaptă un eveniment din coada de alarme
        TemperatureEvents temperatureEvent = TemperatureEvents(0, 0, 0);
        xQueueReceive(alarmEvents, &temperatureEvent, portMAX_DELAY);

        //   - dacă de la ultima alarmă publicată au trecut mai puțin de
        //     MIN_INTERARRIVAL_MS, respinge evenimentul (afișați un mesaj pe Serial)
        uint32_t sinceLast = temperatureEvent.timestamp - lastRelease;
        if (sinceLast < MIN_INTERARRIVAL_MS)
        {
            Serial.printf("   RESPINS: au trecut doar %lu ms (interval minim %lu ms)\n",
                          (unsigned long)sinceLast, (unsigned long)MIN_INTERARRIVAL_MS);
            continue;
        }
        lastRelease = temperatureEvent.timestamp;
        //   - altfel publică: mqtt_publish("alarm", "deviceId;timestamp;temperatura;prag")
        //   Pentru intervale folosiți un timp monoton, millis(), nu epoch_ms().
        char mesaj[80];
        snprintf(mesaj, sizeof(mesaj), "%s;%lld;%.2f;%.2f", DEVICE_ID, temperatureEvent.timestampEpoch, temperatureEvent.temperature, temperatureEvent.threshold);
        mqtt_publish("alarm", mesaj);
    }
}
// TODO 3: TASK APERIODIC, execută comenzile primite de la business service.
void run_AperiodicCommandTask(void *arg)
{
    for (;;)
    {
        //   Comenzile ajung în command_received() (mai jos), care le pune în coada de
        //   comenzi; fiecare comandă eliberează un job al taskului:
        //     SET_INTERVAL <ms>       schimbă perioada T a taskului periodic
        //     SET_THRESHOLD <valoare> schimbă pragul de alarmă
        //     RESET                   readuce perioada și pragul la valorile inițiale
        //     STATUS                  raportează perioada și pragul curente
        char command[COMMAND_MAX_LEN]={};
        xQueueReceive(commands, &command, portMAX_DELAY);
        int commandTypeMaxLen =14;
        char commandType[commandTypeMaxLen];
        int i = 0;
        while (command[i] != ' ' && command[i] != '\0' && i < commandTypeMaxLen-1)
        {
            commandType[i] = command[i];
            i++;
        }
        commandType[i] = '\0';

        char commandValue[COMMAND_MAX_LEN];
        if (command[i] == ' ')
        {
            i++;
            int j = 0;
            while (command[i] != '\0' && j <= COMMAND_MAX_LEN - sizeof(commandType) - 1)
            {
                commandValue[j] = command[i];
                i++;
                j++;
            }
            commandValue[j] = '\0';
        }
        else
        {
            commandValue[0] = '\0';
        }

        /// vvvv????????????????????????????????????????????vvv
        if (strcmp(commandType, "SET_INTERVAL") == 0 && isNaturalNumber(commandValue))
        {
            uint32_t newPeriod = atoll(commandValue);
            setPeriodMs(newPeriod);
            char statusMessage[80];
            snprintf(statusMessage, sizeof(statusMessage), "%s;%lld;OK SET_INTERVAL %lu", DEVICE_ID, epoch_ms(), (unsigned long)newPeriod);
            mqtt_publish("status", statusMessage);
        }
        else if (strcmp(commandType, "SET_THRESHOLD") == 0 && isRealNumber(commandValue))
        {
            float newThreshold = atof(commandValue);
            setThreshold(newThreshold);
            char statusMessage[80];
            snprintf(statusMessage, sizeof(statusMessage), "%s;%lld;OK SET_THRESHOLD %.2f", DEVICE_ID, epoch_ms(), newThreshold);
            mqtt_publish("status", statusMessage);
        }
        else if (strcmp(commandType, "RESET") == 0)
        {
            setPeriodMs(PERIOD_MS_INITIAL);
            setThreshold(THRESHOLD_INITIAL);
            char statusMessage[80];
            snprintf(statusMessage, sizeof(statusMessage), "%s;%lld;OK RESET", DEVICE_ID, epoch_ms());
            mqtt_publish("status", statusMessage);
        }
        else if (strcmp(commandType, "STATUS") == 0)
        {
            uint32_t currentPeriod = getPeriodMs();
            float currentThreshold = getThreshold();
            char statusMessage[80];
            snprintf(statusMessage, sizeof(statusMessage), "%s;%lld;STATUS %lu %.2f", DEVICE_ID, epoch_ms(), (unsigned long)currentPeriod, currentThreshold);
            mqtt_publish("status", statusMessage);
        }
        else
        {
            char errorMessage[80];
            snprintf(errorMessage, sizeof(errorMessage), "%s;%lld;ERROR %s", DEVICE_ID, epoch_ms(), commandType);
            mqtt_publish("status", errorMessage);
        }
        /// ^^^????????????????????????????????????????????^^^

        //   După fiecare comandă publică o confirmare:
        //     mqtt_publish("status", "deviceId;timestamp;OK SET_INTERVAL 1000")
        //   O comandă necunoscută sau un parametru invalid nu oprește taskul: se
        //   publică "ERROR <motiv>".
    }
}

bool isNaturalNumber(const char *str)
{
    if (str == NULL || *str == '\0' || *str == '-' || *str == '0')
        return false;
    if (*str == '+')
        str++;
    if (*str == '\0' || *str == '0')
        return false;
    uint32_t numberTillNow = 0;
    while (*str)
    {
        if (!isdigit(*str))
            return false;

        uint32_t digit = *str - '0';
        if (numberTillNow > (UINT32_MAX - digit) / 10)
            return false; // Overflow

        numberTillNow = numberTillNow * 10 + digit;
        str++;
    }
    return true;
}
bool isRealNumber(const char *str)
{
    if (str == NULL || *str == '\0')
        return false;
    if (*str == '-' || *str == '+')
        str++;
    if (*str == '\0')
        return false;
    bool hasDecimalPoint = false;
    bool hasDigitbeforeDecimal = false;
    bool hasDigitAfterDecimal = false;
    while (*str)
    {
        if (*str == '.')
        {
            if (hasDecimalPoint)
                return false; // More than one decimal point
            hasDecimalPoint = true;
        }

        else
        {
            if (!isdigit(*str))
                return false; // Not a digit or decimal point

            if (!hasDecimalPoint)
                hasDigitbeforeDecimal = true;
            else
                hasDigitAfterDecimal = true;
        }
        str++;
    }
    return hasDigitbeforeDecimal || hasDigitAfterDecimal;
}

// Apelată de clientul MQTT pentru fiecare comandă primită (vezi connection.h).
void command_received(const char *text)
{
    char command[COMMAND_MAX_LEN];
    strncpy(command, text, COMMAND_MAX_LEN - 1);
    // TODO 3: puneți comanda în coada de comenzi, fără să vă blocați
    //   (timeout 0). text este valid doar în timpul acestui apel: elementul
    //   cozii trebuie să conțină textul însuși (de exemplu un struct cu
    //   char text[COMMAND_MAX_LEN]), nu un pointer.
    if (command[0] != '\0')
    {
        xQueueSend(commands, command, 0);
        Serial.printf("[mqtt] comandă primită: %s\n", command);
    }
}

void setup()
{
    Serial.begin(115200);
    // Fără așteptare la scriere: cu placa la USB, dar fără Serial Monitor deschis,
    // fiecare Serial.printf() ar aștepta până la 2 s să fie citit și ar întârzia
    // taskul care scrie. Așa, textul pe care nu-l citește nimeni se pierde.
    Serial.setTxTimeoutMs(0);
    uint32_t t = millis();
    while (!Serial && millis() - t < 3000)
        delay(10); // așteptăm Serial Monitor, cel mult 3 s
    delay(200);
    Serial.println();
    Serial.printf("=== tema 02: STUDENT %s, DEVICE_ID %s ===\n", STUDENT, DEVICE_ID);

    wifi_connect();
    time_sync();

    // TODO 0 (a): creați cele două cozi (xQueueCreate): coada de alarme și
    //   coada de comenzi. Înainte de mqtt_start(): de acolo încolo pot sosi
    //   comenzi, iar command_received() le pune în coada de comenzi.
    alarmEvents = xQueueCreate(10, sizeof(TemperatureEvents));
    commands = xQueueCreate(10, sizeof(char[COMMAND_MAX_LEN]));

    mqtt_start();

    // TODO 0 (b): porniți cele trei taskuri (xTaskCreate). După mqtt_start(),
    //   ca primele mesaje publicate să nu se piardă. Alegeți prioritățile
    //   între 2 și 19: peste loopTask (1), sub taskurile sistemului pentru
    //   evenimente și Wi-Fi (20, 23). Stiva, în octeți: cel puțin 4096.
    xTaskCreate(run_PeriodicTelemetryTask, "PeriodicTelemetryTask", 4096, NULL, 5, NULL);
    xTaskCreate(run_SporadicAlarmTask, "SporadicAlarmTask", 4096, NULL, 5, NULL);
    xTaskCreate(run_AperiodicCommandTask, "AperiodicCommandTask", 4096, NULL, 5, NULL);
}

void loop()
{
    // Totul se face în taskuri: loop() nu are nimic de făcut.
    delay(1000);
}
