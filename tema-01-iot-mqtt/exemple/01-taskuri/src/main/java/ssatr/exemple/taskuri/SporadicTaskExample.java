package ssatr.exemple.taskuri;

import java.util.Random;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;

/**
 * TASK SPORADIC
 * <p>
 * Un task sporadic este declanșat de evenimente, dar între două eliberări
 * trece cel puțin un interval cunoscut: intervalul minim între sosiri
 * (minimum inter-arrival time). Momentele exacte nu se cunosc, dar densitatea
 * lor maximă, da. Termenul-limită D poate fi hard.
 * Exemplu tipic: o alarmă declanșată când temperatura depășește un prag.
 * <p>
 * Pentru analiză, un task sporadic se tratează ca un task periodic cu perioada
 * egală cu intervalul minim între sosiri.
 * <p>
 * Intervalul minim trebuie impus de hardware sau de cod, nu doar sperat.
 * Java nu îl impune, așa că îl verificăm noi:
 * - un thread "generator" produce evenimente la momente aleatoare
 * (unele prea apropiate unul de altul);
 * - un thread "handler" eliberează un job pentru fiecare eveniment și le
 * RESPINGE pe cele care încalcă intervalul minim între sosiri;
 * - pentru fiecare job, verificăm dacă timpul de răspuns R <= D.
 */
public class SporadicTaskExample {

    static final long MIN_INTERARRIVAL_MS = 1000;
    static final long DEADLINE_MS = 300;
    static final int EVENTS = 12;

    public static void main(String[] args) throws InterruptedException {
        BlockingQueue<Event> queue = new LinkedBlockingQueue<>();

        Thread handler = new Thread(new AlarmHandler(queue), "handler-sporadic");
        handler.setDaemon(true);   // se oprește automat când se termină main
        handler.start();

        // main joacă rolul generatorului de evenimente
        Random random = new Random();
        for (int i = 1; i <= EVENTS; i++) {
            Thread.sleep(200 + random.nextInt(1500));
            Event e = new Event(i, System.currentTimeMillis());
            System.out.printf("[generator] sosește evenimentul #%d%n", e.id);
            queue.put(e);
        }

        Thread.sleep(1000);   // lăsăm handler-ul să termine ultimul job
        System.out.println("Gata.");
    }

    /**
     * Un eveniment: un număr de ordine și momentul sosirii lui.
     */
    static class Event {
        final int id;
        final long time;

        Event(int id, long time) {
            this.id = id;
            this.time = time;
        }
    }

    /**
     * Taskul sporadic: tratează alarmele, respectând intervalul minim între sosiri.
     */
    static class AlarmHandler implements Runnable {
        private final BlockingQueue<Event> queue;
        private final Random random = new Random();
        private long lastRelease = 0;

        AlarmHandler(BlockingQueue<Event> queue) {
            this.queue = queue;
        }

        @Override
        public void run() {
            try {
                while (true) {
                    Event e = queue.take();   // așteaptă (blocat) următorul eveniment

                    long sinceLast = e.time - lastRelease;
                    if (sinceLast < MIN_INTERARRIVAL_MS) {
                        System.out.printf("   [sporadic] #%d RESPINS: au trecut doar %d ms (interval minim %d ms)%n",
                                e.id, sinceLast, MIN_INTERARRIVAL_MS);
                        continue;
                    }
                    lastRelease = e.time;

                    // calculul jobului: tratarea alarmei (simulată)
                    Thread.sleep(50 + random.nextInt(300));

                    long responseTime = System.currentTimeMillis() - e.time;
                    String status = responseTime <= DEADLINE_MS ? "termen respectat" : "TERMEN RATAT";
                    System.out.printf("   [sporadic] #%d timp de răspuns R = %d ms -> %s (D = %d ms)%n",
                            e.id, responseTime, status, DEADLINE_MS);
                }
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
            }
        }
    }
}
