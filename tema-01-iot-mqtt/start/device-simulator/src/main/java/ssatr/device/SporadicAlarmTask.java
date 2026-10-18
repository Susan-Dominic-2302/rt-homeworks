package ssatr.device;

import java.util.Locale;
import java.util.concurrent.BlockingQueue;

/**
 * TASK SPORADIC: tratează alarmele de temperatură.
 * <p>
 * Evenimentele sosesc în coada alarmEvents (puse de taskul periodic).
 * Intervalul minim între sosiri este MIN_INTERARRIVAL_MS: între două eliberări
 * ale taskului (două alarme publicate) trebuie să treacă cel puțin atât.
 * Intervalul este impus de cod: evenimentele care sosesc mai des sunt
 * respinse, ca să nu fie inundat brokerul.
 */
public class SporadicAlarmTask implements Runnable {

    static final long MIN_INTERARRIVAL_MS = 5000;

    private final String deviceId;
    private final MqttConnection mqtt;
    private final BlockingQueue<TemperatureEvent> alarmEvents;
    private long lastRelease = 0;


    public SporadicAlarmTask(String deviceId, MqttConnection mqtt, BlockingQueue<TemperatureEvent> alarmEvents) {
        this.deviceId = deviceId;
        this.mqtt = mqtt;
        this.alarmEvents = alarmEvents;
    }

    @Override
    public void run() {
        // TODO 2: implementați taskul sporadic:
        try {

            for (; ; ) {

                //   - așteptați un eveniment: alarmEvents.take()
                TemperatureEvent temperatureEvent = alarmEvents.take();

                //   - dacă de la ultima eliberare au trecut mai puțin de
                //     MIN_INTERARRIVAL_MS, respingeți evenimentul (afișați un mesaj)
                long sinceLast = temperatureEvent.time - lastRelease;
                if (sinceLast < MIN_INTERARRIVAL_MS) {
                    System.out.printf("   RESPINS: au trecut doar %d ms (interval minim %d ms)%n",
                            sinceLast, MIN_INTERARRIVAL_MS);
                    continue;
                }
                lastRelease = temperatureEvent.time;
                //   - altfel publicați pe Config.alarmTopic(deviceId) mesajul
                //     "deviceId;timestamp;temperatura;prag"
                String topic = Config.alarmTopic(deviceId);
                String timestamp = String.valueOf(System.currentTimeMillis());
                String temperatura = String.format(Locale.US, "%.2f", temperatureEvent.temperature);
                String prag = String.format(Locale.US, "%.2f", temperatureEvent.threshold);
                String mesaj = deviceId + ";" + timestamp + ";" + temperatura + ";" + prag;
                mqtt.publish(topic, mesaj);
                //     (pragul curent poate fi transmis împreună cu temperatura sau
                //      citit din PeriodicTelemetryTask; alegeți și justificați în raport)
                //
                // Vezi exemplul exemple/01-taskuri/.../SporadicTaskExample.java
            }
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
        }
    }
}
