// Semafor pentru pietoni, cu buton, pe ESP32-C3 Super Mini.
//
// Placa este controlerul semaforului; lămpile sunt desenate de dashboard, care
// le aprinde după mesajele "lights" publicate de placă. Patru activități:
//
//   ISR buton BOOT ----+
//                      +--(coada butonului)--> task sporadic
//   command_received --+   (BUTTON)                 |
//                                                   v
//                                         [ starea semaforului ] <-- mutex
//                                            ^          |
//                                            |          v
//                         task periodic de control   task periodic de ieșire
//                         (fazele, T = 100 ms)       (lămpile, T = 500 ms) --> lights
//
// Gata implementate (nu trebuie modificate):
//   config.h         setările: Wi-Fi, broker, STUDENT, pinii, perioadele, duratele fazelor
//   connection.h/cpp Wi-Fi, ora exactă (SNTP), clientul MQTT, mqtt_publish(), epoch_ms()
//
// Așa cum este, schița se conectează la Wi-Fi și la broker și afișează pe
// Serial comenzile primite. Restul îl scrieți voi: TODO 0 ... TODO 3.
//
// Placa: ESP32C3 Dev Module, USB CDC On Boot: Enabled. Serial Monitor: 115200 baud.

#include "config.h"
#include "connection.h"

// Fazele semaforului (vezi tabelul și diagrama din README.md). Numele din
// PHASE_NAMES sunt cele din mesajul "lights", în aceeași ordine ca în enum.
enum Phase
{
    CAR_GREEN,
    CAR_YELLOW,
    ALL_RED,
    PED_GREEN
};
const char *const PHASE_NAMES[] = {"CAR_GREEN", "CAR_YELLOW", "ALL_RED", "PED_GREEN"};

// Culorile unei lămpi, cu numele din mesajul "lights".
enum Light
{
    RED,
    YELLOW,
    GREEN
};
const char *const LIGHT_NAMES[] = {"RED", "YELLOW", "GREEN"};

// Done TODO 0 (c): STAREA SEMAFORULUI, partajată de taskuri și protejată de un
//   mutex (xSemaphoreCreateMutex). De exemplu un struct cu:
struct StareaSemaforului
{
    //   - faza curentă
    Phase areFazaCurenta;
    //   - momentul intrării în faza curentă (millis()), din care se calculează
    //     cât a durat faza și cât mai are
    uint32_t aIntratInFazaCurenta;
    //   - cererea pietonului (lampa AȘTEPTAȚI): setată de taskul sporadic,
    //     ștearsă de taskul de control la intrarea în PED_GREEN
    bool areOCerereDeLaPieton;
    //   Mutexul se ține cât mai puțin: în el doar citiți sau scrieți câmpurile
    //   (eventual copiați starea într-o variabilă locală). Fără mqtt_publish(),
    //   Serial sau așteptări cât timp țineți mutexul.
    StareaSemaforului() : areFazaCurenta(CAR_GREEN), aIntratInFazaCurenta(millis()), areOCerereDeLaPieton(false) {}
};

struct Comenzi
{
    char command[COMMAND_MAX_LEN];
    uint32_t timestamp;
};
StareaSemaforului semaforul;

SemaphoreHandle_t stareaMutex;
QueueHandle_t buttonQueue;

// Done TODO 1: TASK PERIODIC DE CONTROL, cu perioada CONTROL_PERIOD_MS.
void run_Task_Periodic_de_Control(void *arg)
{
    uint32_t start = millis();
    bool fazaCurentaSaSchimbat = false;
    for (;;)
    {
        // TODO 1: luați mutexul, citiți starea, decideți dacă treceți în faza
        //   următoare și actualizați starea. Eliberați mutexul.
        //   La fiecare job, cu mutexul luat, decide dacă faza curentă s-a terminat și
        //   trece în faza următoare, după tabelul din README.md:
        xSemaphoreTake(stareaMutex, portMAX_DELAY);

        switch (semaforul.areFazaCurenta)
        {
        //     CAR_GREEN  -> CAR_YELLOW  după CAR_GREEN_MIN_MS, doar dacă există o cerere
        case CAR_GREEN:
            if (millis() - semaforul.aIntratInFazaCurenta >= CAR_GREEN_MIN_MS && semaforul.areOCerereDeLaPieton)
            {
                semaforul.areFazaCurenta = CAR_YELLOW;
                semaforul.aIntratInFazaCurenta = millis();
                fazaCurentaSaSchimbat = true;
            }
            break;

            //     CAR_YELLOW -> ALL_RED     după CAR_YELLOW_MS
        case CAR_YELLOW:
            if (millis() - semaforul.aIntratInFazaCurenta >= CAR_YELLOW_MS)
            {
                semaforul.areFazaCurenta = ALL_RED;
                semaforul.aIntratInFazaCurenta = millis();
                fazaCurentaSaSchimbat = true;
            }
            break;

            //     ALL_RED    -> PED_GREEN   după ALL_RED_MS; aici se șterge cererea pietonului
        case ALL_RED:
            if (millis() - semaforul.aIntratInFazaCurenta >= ALL_RED_MS)
            {
                semaforul.areFazaCurenta = PED_GREEN;
                semaforul.aIntratInFazaCurenta = millis();
                semaforul.areOCerereDeLaPieton = false;
                fazaCurentaSaSchimbat = true;
            }
            break;

            //     PED_GREEN  -> CAR_GREEN   după PED_GREEN_MS
        case PED_GREEN:
            if (millis() - semaforul.aIntratInFazaCurenta >= PED_GREEN_MS)
            {
                semaforul.areFazaCurenta = CAR_GREEN;
                semaforul.aIntratInFazaCurenta = millis();
                fazaCurentaSaSchimbat = true;
            }
        }
        xSemaphoreGive(stareaMutex);
        if (fazaCurentaSaSchimbat)
        {
            fazaCurentaSaSchimbat = false;
            Serial.printf("[control] faza curentă: %s\n", PHASE_NAMES[semaforul.areFazaCurenta]);
        }
        xTaskDelayUntil(&start, CONTROL_PERIOD_MS);
    }

    //   La fiecare schimbare de fază notați momentul intrării în faza nouă.
    //   Taskul nu publică nimic: doar actualizează starea. După ce eliberează
    //   mutexul, afișează pe Serial faza nouă, dacă s-a schimbat.
    //   Eliberările respectă grila r_k = r_0 + kT: așteptare absolută (xTaskDelayUntil).
    //   Durata unei faze se măsoară cu un timp monoton (millis()), nu cu epoch_ms().
}

// TODO 2: TASK PERIODIC DE IEȘIRE, cu perioada OUTPUT_PERIOD_MS.
void run_Task_Periodic_de_Iesire(void *arg)
{
    uint32_t start = millis();
    for (;;)
    {
        // TODO 2: luați mutexul, citiți starea, calculați culorile lămpilor și
        //   secundele rămase din verdele pietonilor. Eliberați mutexul.
        //   La fiecare job:
        //   - copiază starea (cu mutexul luat, cât mai scurt)
        xSemaphoreTake(stareaMutex, portMAX_DELAY);
        StareaSemaforului semaforulLocal = semaforul;
        xSemaphoreGive(stareaMutex);

        //   - stabilește culorile celor două lămpi din faza curentă (tabelul din README.md)
        //   - calculează secundele rămase din verdele pietonilor: în PED_GREEN,
        //     rotunjite în sus (7,2 s rămase -> 8); 0 în celelalte faze
        //   - publică: mqtt_publish("lights", "deviceId;timestamp;faza;masini;pietoni;asteptati;secunde")
        //     de exemplu "esp32-a1b2c3;1727600000000;PED_GREEN;RED;GREEN;0;8"
        char mesaj[80];
        switch (semaforulLocal.areFazaCurenta)
        {
        case CAR_GREEN:
            snprintf(mesaj, sizeof(mesaj), "%s;%lld;%s;%s;%s;%d;%d", DEVICE_ID, epoch_ms(), PHASE_NAMES[semaforulLocal.areFazaCurenta], LIGHT_NAMES[GREEN], LIGHT_NAMES[RED], semaforulLocal.areOCerereDeLaPieton, 0);
            break;
        case CAR_YELLOW:
            snprintf(mesaj, sizeof(mesaj), "%s;%lld;%s;%s;%s;%d;%d", DEVICE_ID, epoch_ms(), PHASE_NAMES[semaforulLocal.areFazaCurenta], LIGHT_NAMES[YELLOW], LIGHT_NAMES[RED], semaforulLocal.areOCerereDeLaPieton, 0);
            break;
        case ALL_RED:
            snprintf(mesaj, sizeof(mesaj), "%s;%lld;%s;%s;%s;%d;%d", DEVICE_ID, epoch_ms(), PHASE_NAMES[semaforulLocal.areFazaCurenta], LIGHT_NAMES[RED], LIGHT_NAMES[RED], semaforulLocal.areOCerereDeLaPieton, 0);
            break;
        case PED_GREEN:
            snprintf(mesaj, sizeof(mesaj), "%s;%lld;%s;%s;%s;%d;%d", DEVICE_ID, epoch_ms(), PHASE_NAMES[semaforulLocal.areFazaCurenta], LIGHT_NAMES[RED], LIGHT_NAMES[GREEN], semaforulLocal.areOCerereDeLaPieton, (int)ceil((PED_GREEN_MS - (millis() - semaforulLocal.aIntratInFazaCurenta)) / 1000.0));
            break;
        }

        //   - aprinde LED-ul de pe placă (LED_PIN, aprins pe LOW) cât timp lampa
        //     AȘTEPTAȚI este aprinsă
        semaforulLocal.areOCerereDeLaPieton ? digitalWrite(LED_PIN, LOW) : digitalWrite(LED_PIN, HIGH);
        mqtt_publish("lights", mesaj);
        Serial.printf("[debug] trimis lights: %s\n", mesaj);

        //   Așteptare absolută (xTaskDelayUntil), ca și la taskul de control.
        xTaskDelayUntil(&start, OUTPUT_PERIOD_MS);
    }
}

// TODO 3: TASK SPORADIC, tratează apăsările butonului pentru pietoni.
void run_Task_Sporadic(void *arg)
{
    uint32_t ultimaApasare = 0;
    for (;;)
    {
        //   Apăsările vin din două surse, în aceeași coadă (coada butonului):
        //   - butonul BOOT de pe placă, printr-o rutină de întrerupere (ISR, mai jos)
        //   - butonul virtual din dashboard: comanda "BUTTON", pusă în coadă de
        //     command_received()
        //   Taskul:
        //   - așteaptă o apăsare din coada butonului
        Comenzi comanda;
        xQueueReceive(buttonQueue, &comanda, portMAX_DELAY);

        if (comanda.timestamp - ultimaApasare < MIN_INTERARRIVAL_MS)
        {
            //   - dacă de la ultima apăsare acceptată au trecut mai puțin de
            //     MIN_INTERARRIVAL_MS, o respinge (mesaj pe Serial). Așa se filtrează și
            //     contactele imperfecte ale butonului (bounce), care produc mai multe
            //     întreruperi la o singură apăsare.
            Serial.printf("[sporadic] apăsare respinsă: au fost %lu ms de ultima apăsare\n", comanda.timestamp - ultimaApasare);
        }
        else
        {
            //   - altfel înregistrează cererea pietonului în starea semaforului (cu mutexul)
            //     și afișează pe Serial că a acceptat-o
            xSemaphoreTake(stareaMutex, portMAX_DELAY);
            semaforul.areOCerereDeLaPieton = true;
            xSemaphoreGive(stareaMutex);
            Serial.printf("[sporadic] apăsare acceptată la %lu ms\n", comanda.timestamp);
            ultimaApasare = comanda.timestamp;
        }
        
    }
}

//   Momentul sosirii se ia când apăsarea intră în coadă (în ISR sau în
//   command_received()), nu când o preia taskul, și se pune în elementul cozii.

// Done TODO 3: RUTINA DE ÎNTRERUPERE a butonului BOOT, înregistrată în setup() cu
//   attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), button_isr, FALLING).
//   Scurtă: pune o apăsare în coada butonului cu xQueueSendFromISR() și atât.
//   Fără Serial, fără mqtt_publish(), fără mutex. Se declară cu IRAM_ATTR:
//
//   void IRAM_ATTR button_isr(void) { ... }
void IRAM_ATTR button_isr(void)
{
    BaseType_t woken = pdFALSE;
    Comenzi comanda;
    comanda.timestamp = millis();
    strncpy(comanda.command, "BUTTON", COMMAND_MAX_LEN - 1);
    xQueueSendFromISR(buttonQueue, &comanda, &woken);
    portYIELD_FROM_ISR(woken);
}

// Apelată de clientul MQTT pentru fiecare comandă primită (vezi connection.h).
void command_received(const char *text)
{
    Serial.printf("[mqtt] comandă primită: %s\n", text);
    // TODO 3: "BUTTON" merge în coada butonului (o apăsare, la fel ca cea din
    //   ISR), fără să vă blocați (timeout 0). Celelalte comenzi se ignoră:
    //   în această temă placa nu are alte comenzi.
    if (strcmp(text, "BUTTON") == 0)
    {
        Comenzi comanda;
        comanda.timestamp = millis();
        strncpy(comanda.command, "BUTTON", COMMAND_MAX_LEN - 1);
        xQueueSend(buttonQueue, &comanda, 0);
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
    Serial.printf("=== tema 03: STUDENT %s, DEVICE_ID %s ===\n", STUDENT, DEVICE_ID);

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH); // LED stins
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    wifi_connect();
    time_sync();

    // Done TODO 0 (a): creați mutexul stării (xSemaphoreCreateMutex) și coada
    //   butonului (xQueueCreate). Înainte de mqtt_start(): de acolo încolo pot
    //   sosi comenzi, iar command_received() pune "BUTTON" în coadă.
    //   Notați și momentul intrării în faza inițială (CAR_GREEN).
    stareaMutex = xSemaphoreCreateMutex();
    buttonQueue = xQueueCreate(10, sizeof(Comenzi));

    mqtt_start();

    // Done TODO 0 (b): porniți cele trei taskuri (xTaskCreate), apoi înregistrați
    //   întreruperea butonului (attachInterrupt). Alegeți prioritățile între
    //   2 și 19 (peste loopTask, sub taskurile sistemului); pentru cele două
    //   taskuri periodice, după regula Rate Monotonic. Stiva, în octeți: cel puțin 4096.
    xTaskCreate(run_Task_Periodic_de_Control, "Task_Periodic_de_Control", 4096, NULL, 15, NULL);
    xTaskCreate(run_Task_Periodic_de_Iesire, "Task_Periodic_de_Iesire", 4096, NULL, 14, NULL);
    xTaskCreate(run_Task_Sporadic, "Task_Sporadic", 4096, NULL, 13, NULL);
    attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), button_isr, FALLING);
}

void loop()
{
    // Totul se face în taskuri: loop() nu are nimic de făcut.
    delay(1000);
}
