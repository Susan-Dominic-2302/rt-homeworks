package ssatr.service;

import java.nio.CharBuffer;
import java.util.HashMap;
import java.util.concurrent.Semaphore;

/**
 * Ultima stare cunoscută a fiecărui device: ultima temperatură, momentul
 * ultimului mesaj, numărul de alarme etc.
 * <p>
 * Este actualizată de thread-urile MessageProcessor și citită de CommandConsole,
 * deci accesul trebuie sincronizat.
 */
public class Dashboard {

    // TODO 5: alegeți structura de date pentru starea device-urilor
    //   (de exemplu un Map<String, ...> cu cheia deviceId)
    //   și adăugați metodele de actualizare apelate din MessageProcessor.
    private HashMap<String, DashboardData> devicesData = new HashMap<>();
    private Semaphore dataMutex = new Semaphore(1);

    class MaxLnData {
        int maxLnDevice = "DEVICE".length();
        int maxLnTemp = "TEMP".length();
        int maxLnUltimulMesaj = "ULTIMUL MESAJ".length();
    }

    /**
     * Afișează un tabel cu starea tuturor device-urilor.
     */
    public void print() throws InterruptedException {
        MaxLnData maxLnData = new MaxLnData();

        // TODO 5: afișați câte un rând pentru fiecare device, de exemplu:
        //   DEVICE     TEMP    ULTIMUL MESAJ   ALARME
        //   device-1   23.41   acum 1 s        2
        dataMutex.acquire();
        for (String deviceId : devicesData.keySet()) {
            DashboardData dashboardData = devicesData.get(deviceId);
            dashboardData.calculeazaUltimulMesaj();
            maxLnData.maxLnDevice = Math.max(deviceId.length(), maxLnData.maxLnDevice);
            maxLnData.maxLnTemp = Math.max(String.valueOf(dashboardData.temp).length(), maxLnData.maxLnTemp);
            maxLnData.maxLnUltimulMesaj = Math.max(("acum  s" + dashboardData.timpulDeLaUltimulMesaj).length(), maxLnData.maxLnUltimulMesaj);
        }
        String DEVICE_TEMP = CharBuffer.allocate(maxLnData.maxLnDevice - "DEVICE".length() + 3).toString().replace('\0', ' ');
        String TEMP_ULTIMULMESAJ = CharBuffer.allocate(maxLnData.maxLnTemp - "TEMP".length() + 3).toString().replace('\0', ' ');
        String ULTIMULMESAJ_ALARME = CharBuffer.allocate(maxLnData.maxLnUltimulMesaj - "ULTIMUL MESAJ".length() + 3).toString().replace('\0', ' ');

        System.out.print("\nDEVICE" + DEVICE_TEMP + "TEMP" + TEMP_ULTIMULMESAJ + "ULTIMUL MESAJ" + ULTIMULMESAJ_ALARME + "ALARME\n");
        for (String deviceId : devicesData.keySet()) {
            DashboardData dashboardData = devicesData.get(deviceId);
            DEVICE_TEMP =
                    CharBuffer.allocate(maxLnData.maxLnDevice - deviceId.length() + 3).toString().replace('\0', ' ');
            TEMP_ULTIMULMESAJ =
                    CharBuffer.allocate(maxLnData.maxLnTemp - String.valueOf(dashboardData.temp).length() + 3).toString().replace('\0', ' ');
            ULTIMULMESAJ_ALARME =
                    CharBuffer.allocate(maxLnData.maxLnUltimulMesaj - (dashboardData.timpulDeLaUltimulMesaj + "acum  s").length() + 3).toString().replace('\0', ' ');

            System.out.print(deviceId + DEVICE_TEMP + dashboardData.temp + TEMP_ULTIMULMESAJ + "acum " + dashboardData.timpulDeLaUltimulMesaj + " s" + ULTIMULMESAJ_ALARME + dashboardData.alarme + "\n");
        }
        dataMutex.release();

        System.out.println("(dashboard neimplementat)");
    }

    public void actualizare(String kind, String mesaj) throws InterruptedException {
        String[] date = mesaj.split(";");
        String deviceId = date[0];
        long timestamp = Long.parseLong(date[1]);
        double temperatura;
        dataMutex.acquire();
        DashboardData dashboardData = devicesData.getOrDefault(deviceId, new DashboardData(0, timestamp, 0));
        dashboardData.ultimulMesaj = System.currentTimeMillis();
        switch (kind) {
            // * telemetry:  deviceId;timestamp;temperatura
            case "telemetry":
                temperatura = Double.parseDouble(date[2]);
                dashboardData.temp = temperatura;
                break;
            // * alarm:      deviceId;timestamp;temperatura;prag
            case "alarm":
                temperatura = Double.parseDouble(date[2]);
                dashboardData.alarme+=1;
                dashboardData.temp = temperatura;

        }
        devicesData.put(deviceId, dashboardData);
        dataMutex.release();
    }
}

