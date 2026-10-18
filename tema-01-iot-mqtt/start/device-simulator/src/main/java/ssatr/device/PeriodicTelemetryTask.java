package ssatr.device;

import java.util.Locale;
import java.util.Random;
import java.util.concurrent.BlockingQueue;

/**
 * TASK PERIODIC, cu perioada T = periodMs: fiecare job citește senzorul și
 * publică valoarea pe topic-ul de telemetrie.
 * <p>
 * Dacă temperatura depășește pragul, taskul NU tratează el alarma, ci doar
 * semnalează un eveniment în coada alarmEvents. Alarma este tratată de
 * taskul sporadic.
 */
public class PeriodicTelemetryTask implements Runnable {

    private final String deviceId;
    private final TemperatureSensor sensor;
    private final MqttConnection mqtt;
    private final BlockingQueue<TemperatureEvent> alarmEvents;
    private final Random random = new Random();


    // volatile: sunt citite aici, dar modificate de taskul aperiodic (alt thread)
    private volatile long periodMs = 2000;
    private volatile double threshold = 28.0;

    public PeriodicTelemetryTask(String deviceId, TemperatureSensor sensor, MqttConnection mqtt,
                                 BlockingQueue<TemperatureEvent> alarmEvents) {
        this.deviceId = deviceId;
        this.sensor = sensor;
        this.mqtt = mqtt;
        this.alarmEvents = alarmEvents;
    }

    @Override
    public void run() {
        long periodNano, start = System.nanoTime();
        // TODO 1: implementați bucla taskului periodic. Fiecare job:
        for (; ; ) {

            //   - citiți temperatura: sensor.read()
            double t = sensor.read();

            //   - publicați pe Config.telemetryTopic(deviceId) mesajul "deviceId;timestamp;temperatura"
            //     (timestamp = System.currentTimeMillis(); temperatura cu punct zecimal,
            //      de ex. String.format(Locale.US, "%.2f", t), ca să poată fi citită
            //      cu Double.parseDouble în business-service)

            String topic = Config.telemetryTopic(deviceId);
            String timestamp = String.valueOf(System.currentTimeMillis());
            String temperatura = String.format(Locale.US, "%.2f", t);
            String mesaj = deviceId + ";" + timestamp + ";" + temperatura;
            mqtt.publish(topic, mesaj);

            //   - dacă temperatura > threshold, puneți valoarea în coada alarmEvents
            double threshold=getThreshold();
            if (t > threshold)
                alarmEvents.add(new TemperatureEvent(t,System.currentTimeMillis(),threshold));

            //
            // Folosiți așteptare absolută, nu relativă: Thread.sleep(periodMs) după
            // fiecare job duce la derivă (perioada reală devine T + C + întârzieri).
            // Calculați eliberarea următorului job pe grilă (next = next + periodMs)
            // și așteptați doar cât a mai rămas până la ea. Pentru grilă folosiți
            // un timp monoton: System.nanoTime().
            periodNano = getPeriodMs() * 1_000_000L;
            long workTimeRelativeToStart = System.nanoTime() - start;
            long workTimeRelativeToLastTaskStartTick = workTimeRelativeToStart % periodNano;
            long waitingTimeBetweenWorkAndNextTaskStartTick = periodNano - workTimeRelativeToLastTaskStartTick;
            try {
                Thread.sleep(waitingTimeBetweenWorkAndNextTaskStartTick / 1_000_000L + random.nextInt(150));
            } catch (InterruptedException e) {
                System.out.println("\n**********************\n" +
                        "Ceva greseala la timpul de asteptare" +
                        "\n**********************\n");
                throw new RuntimeException(e);
            }

            //
            // periodMs se poate schimba în timpul rulării (comanda SET_INTERVAL),
            // deci citiți-l la fiecare job.
        }
    }

    public void setPeriodMs(long periodMs) {
        this.periodMs = periodMs;
    }

    public void setThreshold(double threshold) {
        this.threshold = threshold;
    }

    public long getPeriodMs() {
        return periodMs;
    }

    public double getThreshold() {
        return threshold;
    }
}
