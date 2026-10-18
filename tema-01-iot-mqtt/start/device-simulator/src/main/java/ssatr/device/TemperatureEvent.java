package ssatr.device;

public class TemperatureEvent {
    final Double temperature;
    final long time;
    final double threshold;

    public TemperatureEvent(Double temperature, long time, double threshold) {
        this.temperature = temperature;
        this.time = time;
        this.threshold = threshold;
    }
}
