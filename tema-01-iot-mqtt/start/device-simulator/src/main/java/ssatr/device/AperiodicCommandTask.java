package ssatr.device;

import java.util.Arrays;
import java.util.concurrent.BlockingQueue;

/**
 * TASK APERIODIC: execută comenzile primite de la business service.
 * <p>
 * Comenzile sosesc oricând, pe MQTT, fără un interval minim între ele, și sunt
 * puse în coada commands. Fiecare comandă eliberează un job al taskului.
 * Comenzi care trebuie suportate:
 * <p>
 * SET_INTERVAL <ms>       schimbă perioada T a taskului periodic
 * SET_THRESHOLD <valoare> schimbă pragul de alarmă
 * RESET                   readuce senzorul la valoarea inițială
 * STATUS                  raportează perioada și pragul curente
 * <p>
 * După executarea fiecărei comenzi, device-ul publică pe Config.statusTopic(deviceId)
 * o confirmare: "deviceId;timestamp;text", de exemplu
 * "device-1;1727600000000;OK SET_INTERVAL 1000".
 */
public class AperiodicCommandTask implements Runnable {

    private final String deviceId;
    private final TemperatureSensor sensor;
    private final MqttConnection mqtt;
    private final BlockingQueue<String> commands;
    private final PeriodicTelemetryTask periodic;

    public AperiodicCommandTask(String deviceId, TemperatureSensor sensor, MqttConnection mqtt,
                                BlockingQueue<String> commands, PeriodicTelemetryTask periodic) {
        this.deviceId = deviceId;
        this.sensor = sensor;
        this.mqtt = mqtt;
        this.commands = commands;
        this.periodic = periodic;
    }

    @Override
    public void run() {
        // TODO 3: implementați taskul aperiodic:
        try {
            for (; ; ) {

                //   - preluați comenzile din coadă: commands.take()
                String command = commands.take();

                //   - separați numele comenzii de parametru: command.split(" ")
                String[] tagValueCommand = command.split(" ");

                //   - executați comanda (periodic.setPeriodMs(...), sensor.reset() etc.)
                //   - publicați confirmarea pe Config.statusTopic(deviceId)
                String timestamp = String.valueOf(System.currentTimeMillis());
                String mesaj = deviceId + ";" + timestamp + ";";
                String topic = Config.statusTopic(deviceId);
                switch (tagValueCommand[0]) {
                    case "SET_INTERVAL":
                        try {
                            periodic.setPeriodMs(Long.parseLong(tagValueCommand[1]));
                            mqtt.publish(topic, mesaj + "OK " + Arrays.toString(tagValueCommand));
                        } catch (NumberFormatException | ArrayIndexOutOfBoundsException ignored) {
                            mqtt.publish(topic, "ERROR " + Arrays.toString(tagValueCommand));
                        }
                        break;

                    case "SET_THRESHOLD":
                        try{
                        periodic.setThreshold(Double.parseDouble(tagValueCommand[1]));
                        mqtt.publish(topic, mesaj + "OK " + Arrays.toString(tagValueCommand));
                        } catch (NumberFormatException | ArrayIndexOutOfBoundsException ignored) {
                            mqtt.publish(topic, "ERROR " + Arrays.toString(tagValueCommand));
                        }
                        break;

                    case "RESET":
                        sensor.reset();
                        mqtt.publish(topic, mesaj + "OK " + tagValueCommand[0]);
                        break;

                    case "STATUS":
                        System.out.println("PERIOADA: "+periodic.getPeriodMs()+"; PRAGUL: "+periodic.getThreshold());
                        mqtt.publish(topic, mesaj + "OK " + tagValueCommand[0]);
                        break;

                    //   - o comandă necunoscută sau un parametru invalid nu trebuie să
                    //     oprească taskul: publicați "ERROR <motiv>"
                    default:
                        mqtt.publish(topic, "ERROR " + Arrays.toString(tagValueCommand));
                        break;
                }

                //
                // Vezi exemplul exemple/01-taskuri/.../AperiodicTaskExample.java
            }
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
        }
    }
}
