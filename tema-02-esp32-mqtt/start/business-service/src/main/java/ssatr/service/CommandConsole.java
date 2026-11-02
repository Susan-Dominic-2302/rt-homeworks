package ssatr.service;

import java.util.Scanner;


/**
 * Consola operatorului: citește comenzi de la tastatură.
 * <p>
 * show                               afișează dashboard-ul
 * <deviceId> <COMANDA> [parametru]   trimite o comandă unui device, de exemplu
 * device-1 SET_INTERVAL 1000
 * help                               afișează comenzile
 * exit                               oprește serviciul
 */
public class CommandConsole implements Runnable {

    private final MqttConnection mqtt;
    private final Dashboard dashboard;

    public CommandConsole(MqttConnection mqtt, Dashboard dashboard) {
        this.mqtt = mqtt;
        this.dashboard = dashboard;
    }

    @Override
    public void run() {
        Scanner scanner = new Scanner(System.in);
        printHelp();

        while (scanner.hasNextLine()) {
            String line = scanner.nextLine().trim();

            if (line.isEmpty()) {
                continue;
            } else if (line.equals("exit")) {
                break;
            } else if (line.equals("help")) {
                printHelp();
            } else if (line.equals("show")) {
                try {
                    dashboard.print();
                } catch (InterruptedException e) {
                    throw new RuntimeException(e);
                }
            } else {
                sendCommand(line);
            }
        }
    }

    private void sendCommand(String line) {
        // TODO 6: separați deviceId de restul liniei
        //   ("device-1 SET_INTERVAL 1000" -> "device-1" și "SET_INTERVAL 1000")
        String[] commandLine = line.split(" ");
        //   și publicați comanda pe Config.commandTopic(deviceId).
        String deviceId = commandLine[0];
        String topic = Config.commandTopic(deviceId);
        if(commandLine.length>1) {
            String command = commandLine[1];
            switch (command) {
                case "SET_INTERVAL":
                case "SET_THRESHOLD":
                    if(commandLine.length<3) break;
                    String value = commandLine[2];
                    command += " " + value;
                case "RESET":
                case "STATUS":
                    mqtt.publish(topic, command);
                    return;
            }
        }
        //   Afișați un mesaj de eroare dacă linia nu are formatul corect.
        System.out.println("FORMAT incorect: " + line);
    }

    private void printHelp() {
        System.out.println("Comenzi: show | <deviceId> SET_INTERVAL <ms> | <deviceId> SET_THRESHOLD <valoare>");
        System.out.println("         <deviceId> RESET | <deviceId> STATUS | help | exit");
    }
}
