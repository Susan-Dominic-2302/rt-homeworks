package ssatr.service;

public class DashboardData {
    //   ---DEVICE----   TEMP    ULTIMUL MESAJ   ALARME
    //   ---device-1--   23.41   acum 1 s        2
    public double temp=0;
    public long timpulDeLaUltimulMesaj=0;
    public long ultimulMesaj=0;
    public int alarme=0;

    public DashboardData(double temp, long ultimulMesaj, int alarme) {
        this.temp = temp;
        this.ultimulMesaj = ultimulMesaj;
        this.alarme = alarme;
    }
    public void calculeazaUltimulMesaj(){
        timpulDeLaUltimulMesaj=(System.currentTimeMillis()-ultimulMesaj)/1000;
    }
}
