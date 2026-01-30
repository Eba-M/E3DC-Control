//
//  Waermepumpe.cpp
//  E3DC-V1
//
//  Created by Eberhard Mayer on 11.09.23.
//


//#include "myHeader.h"
#include "awattar.hpp"
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <cstring>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <array>
#include <algorithm>
#include <vector>
#include "cJSON.h"
#include <fcntl.h>
/*
Steuerung einer BWWP über Tasmota
Zwischen Sonnenaufgang und Sonnenuntergang wird die BWWP über PV-Überschuss gesteuert.
 In der Nacht werden zu den günstigsten Zeiten (tibber) für eine bestimmte Stundenzahl die BWWP
 angesteuert.
BWWPTasmota = // String zur Ansteuerung der Tasmota-Steckdose
BWWPTasmotahour = // Anzahl der taglichen Soll Stunden
Die kummulierte PV-Überschusszeit wird auf die gesamtlaufzeit angerechnet
*/
static time_t rawtime;
struct tm * timeinfo;
static int status = -1; // 0 = tag, 1 = nacht
static int Tasmotastatus = -1; // 0 = aus, 1 = ein
static time_t ontime;
static bool debug;
char buf[100];

int debugout(char buffer [100])
//        if (debug)
{
    FILE *fp;

    timeinfo = localtime (&rawtime);
    fp = fopen("debug1.out","a");
    fprintf(fp,"%s %s\n",asctime(timeinfo),buffer);
    fclose(fp);
    return 0;
}

int MQTTsend1(char host[20],char buffer[127])

{
    char cbuf[768];
    if (strcmp(host,"0.0.0.0")!=0) //gültige hostadresse
    {
        sprintf(cbuf, "mosquitto_pub -r -h %s -t %s", host,buffer);
        int ret = system(cbuf);
        if (debug)
        debugout(cbuf);
        return ret;
    } else
    return(-1);
}

int tasmotaon1(e3dc_config_t &e3dc)
{
    char buf[127];
    sprintf(buf,"cmnd/%s/POWER -m ON",e3dc.BWWPTasmota);
    if (Tasmotastatus <= 0){
        MQTTsend1(e3dc.mqtt2_ip,buf);
    }
        Tasmotastatus = 1;
    return 0;
}
int tasmotaoff1(e3dc_config_t &e3dc)
{
    char buf[127];
    sprintf(buf,"cmnd/%s/POWER -m OFF",e3dc.BWWPTasmota);
    if (Tasmotastatus == 1)
    MQTTsend1(e3dc.mqtt2_ip,buf);
    Tasmotastatus = 0;
    return 0;
}

void bwwptasmota(std::vector<watt_s> &w,e3dc_config_t &e3dc,int sunrise, int sunset,int ireq_Heistab)

{
    if (debug)
        printf("BT1");
    static std::vector<ch_s> ch;
    static ch_s cc;
    static int dauer = -1;
    time(&rawtime);
    
    if (ireq_Heistab>500)
    {
        ontime = rawtime;
        if (Tasmotastatus <= 0)
            tasmotaon1(e3dc);
    }
    if (rawtime%(24*3600)/60>sunrise&&rawtime%(24*3600)/60<sunset) 
        status = 0;
    if ((rawtime%(24*3600)/60>sunset||rawtime%(24*3600)/60<sunrise)&&(status<1||e3dc.BWWPTasmotaDauer!=dauer)) // wechsel tag/nacht
    {
        status = 1;
        dauer = e3dc.BWWPTasmotaDauer;
        ch.clear();
        int stunden = ((sunrise+24*60)-sunset)/60;
        for(int j=0;j<w.size()&&ch.size()<stunden;j++)
        {
            cc.hh = w[j].hh;
            cc.pp = w[j].pp;
            int min = cc.hh%(24*3600)/60;
            if ((min>sunset||min<sunrise)&&cc.hh%3600==0)
                ch.push_back(cc);
        }
        std::sort(ch.begin(), ch.end(), [](const ch_s& a, const ch_s& b) {
            return a.pp < b.pp;});
        while (ch.size()>e3dc.BWWPTasmotaDauer)
        {
            ch.erase(ch.end()-1);
        }
        std::sort(ch.begin(), ch.end(), [](const ch_s& a, const ch_s& b) {
            return a.hh < b.hh;});
        
    }
    
    if (ch.size() > 0)
    {
        
        if (ch[0].hh+3600 < rawtime)
        {
            ch.erase(ch.begin());
            if (ch.size() == 0&&Tasmotastatus > 0)
                tasmotaoff1(e3dc);
            return;
        }
        if (ch[0].hh <= rawtime&&Tasmotastatus <= 0)
        {
            tasmotaon1(e3dc);
        }
    }
    if ((ch.size() == 0||ch[0].hh > rawtime)&&Tasmotastatus > 0&&((rawtime-ontime)>600))
    {
        tasmotaoff1(e3dc);
    }
    if (ch.size() > 0)
        printf("Tasmo %i",(ch[0].hh%(24*3600)/3600));
    else
        printf("Tasmo %i",(rawtime-ontime));

};



// Wärmepumpe   Die Wärmepumpe soll kostenoptimiert betrieben werden.
// Um den Wärmebedarf vom Haus zu ermitteln werden die Wetterdaten der
// kommenden 48h herangezogen. Zusammen mit den Strompreisen werden die günstigsten
// Betriebsstunden der Wärmepumpe berechnet und die Wärmepumpe über MQTT entsprechend angesteuert.
// Die Heizkreise der oekofen werden zeitgleich mit dem Betrieb der WP geschaltet.
// Reicht die Heizleistung der WP nicht aus oder ist der Betrieb der oekofen kostengünstiger,
// so wird diese entsprechend zugeschaltet. Zu diesem Zweck wird die oekofen über Modbus/TCP eingebunden.
// Auf desem Weg stehen auch Puffertemperaturen etc. der oekofen zur Verfügung.
//
// Der Prozess call_wp wird beim Starten des des Programms und dann zu jeder vollen Stunden aufgerufen
// Es werden aus Wettervorhersagen und den Strompreisen den Wärmebedarf der nächsten 48h ermittelt
// und die Steuerzeiten der WP bis zum Ende der nächsten Preisperiode der EPEX.



static wetter_s we;
static float fusspunkt = 28; // Fusspunkt bei 15°

static float endpunkt = 50;  // Endpunkt bei -15°
static float absolutenull = 273; // absoluter Nullpunkt 0K
static float cop,wm,wp;
static time_t oldhour = 0;
static int oldwsize = -1;


// static float ftemp;
//mewp(w,wetter,fatemp,sunriseAt,e3dc_config);       // Ermitteln Wetterdaten

void mewp(std::vector<watt_s> &w,std::vector<wetter_s>&wetter,float ftemp[],const size_t &len,float &fatemp,float &cop, int sunrise, int sunset,e3dc_config_t &e3dc, float soc, int ireq_Heistab, float zuluft,float notstromreserve,int32_t HeatStat) {
    time_t rawtime;
    struct tm * ptm;
    time(&rawtime);
    float anforderung;
    ptm = gmtime (&rawtime);
    debug = e3dc.debug;
    if (e3dc.BWWPTasmotaDauer > 0)
    bwwptasmota(w,e3dc,sunrise,sunset,ireq_Heistab);
    
    if (oldhour < 0 && soc<0) {
        return;
    }
    // Ermitteln COP auf Basis der Zuluft WP
    if (zuluft>-99)
        cop = ((absolutenull+zuluft)/(((-fusspunkt+endpunkt)/(e3dc.WPHeizgrenze+15))*(e3dc.WPHeizgrenze-zuluft)+fusspunkt))*.6;

// Jede Stunde oder wenn neue Börsenstrompreise verfügbar sind auch früher
    if 
        ((ptm->tm_min%15==1||oldhour==0||oldwsize < w.size())
         &&  //2 min im 15min Intervall
        (rawtime-oldhour)>60
        )
    {
        if (w.size()>0)
        {
            oldhour = rawtime;
            oldwsize = w.size();
        }
        FILE * fp;
        FILE * fp1;
        char path [65000];
        char line[256];
        char value[25];
        char var[25];

        if (e3dc.debug) printf("NW1\n");

        if (e3dc.openmeteo)
        {
/*
            sprintf(line,"curl -s -X GET 'https://api.open-meteo.com/v1/forecast?latitude=%f&longitude=%f&minutely_15=temperature_2m&timeformat=unixtime&forecast_minutely_15=192'",e3dc.hoehe,e3dc.laenge);
            fp = NULL;
            fp = popen(line, "r");
            int fd = fileno(fp);
            int flags = fcntl(fd, F_GETFL, 0);
            flags |= O_NONBLOCK;
            fcntl(fd, F_SETFL, flags);
            
            int timeout = 0;
            while (fgets(path, 65000, fp) == NULL&&timeout < 30)
            {
                sleep(1);
                timeout++;
            }
            if (timeout >= 30) return;
            {
                const cJSON *item = NULL;
                const cJSON *item1 = NULL;
                const cJSON *item2 = NULL;
                
                std::string feld;
                cJSON *wolf_json = cJSON_Parse(path);
                feld = "minutely_15";
                char * c = &feld[0];
                item = cJSON_GetObjectItemCaseSensitive(wolf_json, c );
                feld = "time";
                item1 = cJSON_GetObjectItemCaseSensitive(item, c );
                feld = "temperature_2m";
                c = &feld[0];
                item2 = cJSON_GetObjectItemCaseSensitive(item, c );
                if (item1!=NULL)
                    item1 = item1->child;
                if (item2!=NULL)
                item2 = item2->child;
                int x1 = 0;
//                wetter.clear();
                fatemp = 0;
                if ((item1!=NULL)&&wetter.size()>0)
                while (wetter[0].hh < item1->valueint&&wetter.size()>0)
                    wetter.erase(wetter.begin());
                while (item1!=NULL)
                {
                    if(x1<wetter.size())
                    {
                        if (wetter[x1].hh == item1->valueint)
                        {
                            wetter[x1].temp = item2->valuedouble;
                        }
                        
                    }
                    else
                    {
                        we.hh = item1->valueint;
                        we.temp = item2->valuedouble;
                        wetter.push_back(we);
                    }
                        fatemp = fatemp + item2->valuedouble;
                        item1 = item1->next;
                        item2 = item2->next;
                        x1++;
                }
                if (fp!=NULL) pclose(fp);

                if (zuluft >-99) // Temperaturabgleich
                {
                    int j1 = (wetter[0].hh%(24*3600));
                    j1 = j1/900+1;
                    ftemp[0] = ftemp[0] - ftemp[j1] + wetter[0].temp - zuluft;
                    ftemp[j1] = wetter[0].temp - zuluft;
                }
                
            }
 */
            if (zuluft >-99&&wetter.size()>0) // Temperaturabgleich
            {
                int j1 = (wetter[0].hh%(24*3600));
                j1 = j1/900+1;
                ftemp[0] = ftemp[0] - ftemp[j1] + wetter[0].temp - zuluft;
                ftemp[j1] = wetter[0].temp - zuluft;

            }
            FILE *fp;

            fp = fopen("temp.txt","a");
            fprintf(fp,"%2.4f\n",ftemp[0]);
            ftemp[0]=0;
            for (int j=1;j<len;j++)
            {
                ftemp[0] = ftemp[0] + ftemp[j];
                fprintf(fp,"%2.4f %2.4f \n",ftemp[0],ftemp[j]);
            }
            fclose(fp);

            if (wetter.size()==0) return;
            for (int j=0;j<wetter.size();j++)
                fatemp = fatemp + wetter[j].temp;
                
            fatemp = fatemp / wetter.size();
            fatemp = fatemp - ftemp[0]/96;
            if (e3dc.debug) printf("NW2\n");
        }
        else
        if (strlen(e3dc.openweathermap)>0)
        {

            sprintf(line,"curl -s -X GET 'https://api.openweathermap.org/data/2.5/onecall?lat=%0.6f&lon=%0.6f&appid=%s&exclude=(current,minutely,alerts)&units=metric' | jq .hourly| jq '.[]' | jq '.dt, .temp, .clouds, .uvi'>wetter.out",e3dc.hoehe,e3dc.laenge,e3dc.openweathermap);
            int res = system(line);
            fp = fopen("wetter.out","r");
            
            if(fp)
            {
                wetter.clear();
                fatemp = 0;
                cop = -1;
                int x1 = 0;
                int x3 = 0;
                
                while (fgets(line, sizeof(line), fp))
                {
                    
                    we.hh = atol(line);
                    if (fgets(line, sizeof(line), fp))
                    {
                        we.temp = atof(line);
                        we.kosten = 0;
                        fatemp = fatemp + we.temp;
                    } else break;
                    if (fgets(line, sizeof(line), fp))
                    {
                        we.sky = atoi(line);
                    } else break;
                    if (fgets(line, sizeof(line), fp))
                    {
                        we.uvi = atof(line);
                    } else break;
                    
                    wetter.push_back(we);
                    x1++;
                }

                fclose(fp);
                fatemp = fatemp / 48;

            }

 }
        if (e3dc.debug) printf("NW3\n");
        if (w.size()==0) return;
//        if (e3dc.unload < 0) return;
        if (e3dc.debug) printf("NW4\n");
        
        
        {
            {
                if (e3dc.WPLeistung>0) {
                    // Ermitteln des Energiebedarfs für die Wärmepumpe
                    // Die Pelletsheizung wird eingesetzt, wenn die Leistung der WP nicht mehr
                    // ausreicht
                    float fbitemp = e3dc.WPHeizgrenze -
                    float(e3dc.WPLeistung/float(e3dc.WPHeizlast/(e3dc.WPHeizgrenze-e3dc.WPNat)));
                    // fbitemp Bivalenztemperator unter dieser Schwelle muss Pellets oder Heizstab
                    // eingesetzt werden EHZ
                    if (wetter.size()>0&&wetter.size()>0&&w[0].hh>wetter[0].hh)
                        wetter.erase(wetter.begin());
                            for (int x1=0;x1<w.size()&&x1<wetter.size();x1++)
//                            if (wetter[x1].temp < e3dc.WPHeizgrenze&&e3dc.WPLeistung>0)

                            {
                                int z1 = 5;

                                if (zuluft > -99&&x1<z1)
//                                for (int z2=0;z2<z1;z2++)
                                    wetter[x1].temp = ((z1-x1)*zuluft+wetter[x1].temp*x1)/z1;

                                float f1=((-fusspunkt+endpunkt)/(e3dc.WPHeizgrenze+15))*(e3dc.WPHeizgrenze-wetter[x1].temp*1.0)+fusspunkt;
                                // Temperaturhub aud -15° bezogen
                                float f2 = ((absolutenull+wetter[x1].temp)/(f1))*.6; // COP
                                if (cop <= 0) cop = f2;
                                
                                // thermische Heizleistung
                                float f3 = ((e3dc.WPHeizgrenze-wetter[x1].temp))*(e3dc.WPHeizlast/(e3dc.WPHeizgrenze-e3dc.WPNat));
                                if (f3 <= 0) 
                                    f3=0.01; // zu warm keine Heizung
                                float f4 = 0;
                                float f5 = f3; // angeforderte Heizleistung
                                // Heizstab verwenden? angeforderte Heizleistung > Nennleistung WP
                                if (f5 > e3dc.WPLeistung) 
                                {
                                    f4 = f5 - e3dc.WPLeistung;
                                    f3 = e3dc.WPLeistung;
                                    if (f4 > e3dc.WPEHZ)
                                        f4 = e3dc.WPEHZ;
                                }
                                // Heizleistung WP unter Nennwert => mehr Heizstab
                                if (f3/f2>e3dc.WPmax)
                                    f4 = f4 + f3 - f2*e3dc.WPmax + e3dc.WPmax;
                                else
                                    f4 = f4 + f3/f2; // benötigte elektrische Leistung;
                                if (e3dc.openmeteo)
                                {
                                    wetter[x1].waerme = f5;
// absoluter Strombedarf speichern unabhängig vom Speicher??
                                    wetter[x1].kosten = f4;
// noch nicht
                                    while (w.size()>0&&(w[0].hh+900<=rawtime))
                                        w.erase(w.begin());
                                    while (wetter.size()>0&&(wetter[0].hh+900<=rawtime))
                                        wetter.erase(wetter.begin());
                                }
                                else
                                    wetter[x1].kosten = f4; // benötigte elektrische Leistung;
                                
                                if ((w.size()>0)&&x1<=w.size())
                                    if (wetter[x1].hh != w[x1].hh)
                                        if (wetter[x1].hh >= w[x1].hh)
                                            
                                        x1 = x1;
                                    if (wetter[x1].hh == w[x1].hh){
                                        // Überprüfen ob WP oder Pelletsheizung günstiger
                                        // Kosten = aktueller Strompreis / COP + EHZ
                                        float f7 =((w[x1].pp/10)*((100+e3dc.AWMWSt)/100)+e3dc.AWNebenkosten);
                                        float f8 =  f7*f4; // kosten = strompreis * elektr. leistung
                                        float f6 =  f8/f5;  // kosten / Wärmebedarf
                                        float f9 = f7/f2;  // Wärmepreis = Stromkosten/COP
                                        wetter[x1].waermepreis = f9;
                                        wetter[x1].cop = f2;
// Es werden immer die gerechneten Werte genommen
// die hochgerechneten Werte werden aus den statistischen Werten herausgerechnet
// Wenn keine tatsächlichen Werte vorliegen wie bei meiner Wolf
//                                        if (not e3dc.WPWolf) // wenn statistik, dann die verlaufswerte nutzen
//                                        if (strcmp(e3dc.shellyEM_ip,"0.0.0.0")==0&&not e3dc.WPWolf)
                                        if (strcmp(e3dc.shellyEM_ip,"0.0.0.0")==0)
                                            wetter[x1].wpbedarf = wetter[x1].kosten/e3dc.speichergroesse*100/4;
                                        
                                        int bHK1on = 0;
                                        int bHK2on = 1;
                                        {
                                            float f1 = w[x1].hh%(24*3600)/3600.0;
                                            if (f1*60>(sunrise+60)&&(f1*60<sunset+60||f1*60<sunrise+120))
                                                bHK1on = 1;
//                                            if (ALV>0)
//                                                bHK1on = 1;
                                            if ((e3dc.WPHK2off>e3dc.WPHK2on)
                                                &&(f1>e3dc.WPHK2off||f1<e3dc.WPHK2on))
                                                bHK2on = 0;
                                            if ((e3dc.WPHK2off<e3dc.WPHK2on)
                                                &&(f1>e3dc.WPHK2off&&f1<e3dc.WPHK2on))
                                                bHK2on = 0;

/*                                            if (not e3dc.WPWolf) // wenn statistik, dann die verlaufswerte nutzen
                                                wetter[x1].wpbedarf = (bHK1on+bHK2on)/2.0*wetter[x1].kosten/e3dc.speichergroesse*100/4;
                                            
 */                                       }
                                        
                                        
                                        static int WPZWE = 0; // ZWE ausgeschaltet
// f6 = Stromkosten kWh Wärmepumpe ohne Berücksichtigung SoC
// ZWE über Temperatur aktiviert
                                        if ((e3dc.WPZWE>-90&&e3dc.WPZWE>wetter[x1].temp-WPZWE
                                            && e3dc.WPZWEPVon<0)
                                            ||
// ZWE über Kosten aktiviert
                                            (e3dc.WPZWEPVon>0&&f6>e3dc.WPZWEPVon) // Hysterese 1Grad
                                            ||
// ZWE über Kosten aktiviert
                                            (e3dc.WPZWEPVon>0&&f9>e3dc.WPZWEPVon)) // Wärmepreis
                                        {
                                            // Pelletskessel oder WPZWE übernimmt und die WP ist aus
                                            WPZWE = 1;
//                                            if (strcmp(e3dc.shellyEM_ip,"0.0.0.0")==0&&not e3dc.WPWolf)
                                            if (strcmp(e3dc.shellyEM_ip,"0.0.0.0")==0)
                                            wetter[x1].wpbedarf = 0;
                                            wetter[x1].kosten = 0;
                                            
                                            // wenn der Wärmepreis der WP günstiger ist als Pellets
                                            if (f9<e3dc.WPZWEPVon)   // ist der Srompreis günstig?
                                                wetter[x1].kosten = f4;
                                                if (not e3dc.statistik) // wenn statistik, dann die verlaufswerte nutzen
                                                {
                                                    if (e3dc.openmeteo)
                                                    wetter[x1].wpbedarf = (f3/f2) /e3dc.speichergroesse*100/4;
                                                else
                                                    wetter[x1].wpbedarf = (f3/f2) /e3dc.speichergroesse*100;
                                                }
                                        } else
                                            WPZWE = 0;
                                    }
                            }

                    // Aus der ermittelte Durchnittstemperatur geht der Wärmebedarf und damit
                    // die Laufdauer der Wärmepumpe vor
                    // Wenn die Laufzeit < 22h ist, beginnt der WP-Start 2h nach Sonnenaufgang
                    // bis dahin werden die Preise aus w.wpbedarf auf 0 gesetzt
                    // Wenn der bHK2on = Heizkörper aktiv ist dann beginn die WP-Start mit bHK2on
                    
                    // Jetzt mit Heizstab und BWWP 9.9.25
                    // mit max. 5 Durchläufen wird die WP Einsatz berechnet
                    // dann wird die BWWP mit P = 500W und COP 3 vergeben
                    // dann Einsatz des Heizstabs mit 3/6/9 kW
                    // dabei soll
                    float waermebedarf = (e3dc.WPHeizgrenze - fatemp)*24; // Heizgrade
                    waermebedarf = (e3dc.WPHeizlast / (e3dc.WPHeizgrenze - e3dc.WPNat)) * waermebedarf;
                    // Heizlast bei -15°
                    float waermebedarf1;
                    if (w.size()>96)
                        waermebedarf1 = waermebedarf = waermebedarf/96*(w.size()-96); // wärmbedarf nach 24h
                    else
                        waermebedarf1 = 0;
                    float diff = float(HeatStat)/3600000.0;
                        waermebedarf = waermebedarf-diff;

                    if (e3dc.WPWolf)
                    {
//                        if (waermebedarf < w.size()*2.5)
                        float dyncop = e3dc.WPDynCop; // Anpassung cop bei minimum- zu normaleistung um 40%
                        int x2 = 0;
                        int x3 = 0;
                        float flowsoc[3]= {0,0,0};
                        float fhighsoc[3] = {0,0,0};
                        int iindxsoc[3]= {0,0,0};
                        time_t itime[2]= {0,0};
                        float fsoc = soc;
/*
                        int z1 = 5;
                        if (zuluft > -99)
                        for (int z2=0;z2<z1;z2++)
                            wetter[z2].temp = ((z1-z2)*zuluft+wetter[z2].temp*z2)/z1;
*/
                        
//                        if (waermebedarf < e3dc.WPLeistung*24)
                        {
                            // Verteilen des Wärmebedarfs auf die Zeiten der günstigsten Erzeugung, d.h. höchste Temperatur
                            std::vector<wetter1_s>wetter1; // Stundenwerte der Börsenstrompreise
                            std::vector<wetter1_s>wetter2; // Stundenwerte der Börsenstrompreise
                            wetter1_s wet;
                            float adj = (1-(1-e3dc.speichereta)/96);
                            {
                                for (int x1=0;x1<w.size()-1&&x1<wetter.size()-1;x1++)
                                {
                                    if (wetter[x1].solar>0)
                                        fsoc = fsoc - wetter[x1].hourly + wetter[x1].solar;
                                    else
                                        fsoc = fsoc - wetter[x1].hourly/e3dc.speichereta - e3dc.speicherev/1000/e3dc.speichergroesse/4;
                                    //                                    fsoc = fsoc * adj;
                                    if (fsoc>100)
                                    {
                                        fhighsoc[x3] = fhighsoc[x3] + fsoc-100;
                                        fsoc = 100;
                                    }
                                    if (fsoc<0) fsoc = 0;
                                    if (wetter[x1].solar==0&&wetter[x1+1].solar>0||x1==w.size()-1)
                                    {
                                        flowsoc[x3] = fsoc;
                                        itime[x3] = wetter[x1].hh+3*3600;
                                        iindxsoc[x3] = x1;
                                        fsoc = 0;
                                        x3++;
                                    }
                                    if (itime[0]>0&&wetter[x1].hh<itime[0]
                                        &&wetter[x1].hourly + wetter[x1].solar - wetter[x1].wpbedarf<0)
                                    {
                                        flowsoc[0] = flowsoc[0] - (wetter[x1].wpbedarf-wetter[x1].solar)/e3dc.speichereta
                                        -e3dc.speicherev/1000/e3dc.speichergroesse/4;
                                        flowsoc[0]= flowsoc[0] * adj;
                                        if (flowsoc[0] < 0)
                                            flowsoc[0] = 0;
                                    }
                                    
                                }
                                fsoc = flowsoc[0];
                                for (int x1=0;x1<w.size()&&x1<wetter.size();x1++)
                                {
                                    wet.x1 = x1;
                                    //                                if (wetter[x1].solar>0)
                                    if (wetter[x1].cop==0)
                                    {
                                        wet.cop = 7;
                                        wetter[x1].cop = 7;
                                    }
                                    else
                                        // cop um 1 erhöhen für minimum Leistung
                                        //                                    wet.cop = wetter[x1].cop+1;
                                        
                                        wet.cop = wetter[x1].cop*(1+dyncop); // COP anheben bei minimum-Leistung
                                    //                                wet.waermepreis = (w[x1].pp*.1*1.19+e3dc.AWNebenkosten)/wet.cop;
                                    //                                if (wetter[x1].hourly+wetter[x1].wpbedarf<wetter[x1].solar)
                                    //                                float ftest = e3dc.WPmin/e3dc.speichergroesse*25;
                                    if (wetter[x1].solar==0)
                                        fsoc = fsoc - wetter[x1].hourly;
                                    if (wetter[x1].hourly+e3dc.WPmin/e3dc.speichergroesse*25<wetter[x1].solar)
                                    {
                                        wet.waermepreis = 10/wet.cop; // solarpreis = 10ct
                                        x2++;
                                        wet.status = 2;   // 2 = PV, 1 = Speicher, 0 = Netz
                                    }
                                    else
                                    {
                                        if (fsoc*e3dc.speichereta-e3dc.AWReserve-notstromreserve>0)
                                        {
                                            wet.waermepreis = 12/wet.cop; // solarpreis = 12ct Wenn aus dem Speicher
                                            wet.status = 1;
                                        }
                                        else
                                        {
                                            wet.waermepreis = (w[x1].pp*.1*(100+e3dc.AWMWSt)/100+e3dc.AWNebenkosten)/wet.cop;
                                            wet.status = 0;
                                        }
                                    }
                                    wetter[x1].wpbedarf=0;
                                    wetter[x1].wwwpbedarf=0;
                                    wetter[x1].heizstabbedarf=0;
                                    wetter[x1].waerme=0;
                                    wetter[x1].waermepreis=wet.waermepreis;
                                    wet.temp = wetter[x1].temp;
                                    if (x1<96)
                                        wetter1.push_back(wet);
                                    else
                                        wetter2.push_back(wet);
                                }
                                
                                //                            waermebedarf= 109;
                                int schleife = 0;
                                for (int y2=0;y2<2;y2++)
                                {
                                    if (y2==1)
                                    {
                                        waermebedarf=waermebedarf1;
                                        wetter1.clear();
                                        wetter1 = wetter2;
                                        wetter2.clear();
                                    }
                                
                                    while (waermebedarf>1)
                                    {
                                        schleife++;
                                        if (schleife>100000)
                                            break;
                                        std::stable_sort(wetter1.begin(), wetter1.end(), [](const wetter1_s& a, const wetter1_s& b)
                                        {
                                            if (a.waermepreis == b.waermepreis)
                                            return a.temp < b.temp;
                                            else
                                            return a.waermepreis < b.waermepreis;
                                        });
                                        {
                                            wet = wetter1[0];
                                            
                                            float av = e3dc.WPLeistung/(wetter[wetter1[0].x1].cop);  // Power bei WP NennLeistung
                                            float leistung = e3dc.WPmin;
                                            float diffleistung = av - leistung;
                                            
                                            {
                                                float wpbedarf = wetter[wetter1[0].x1].wpbedarf;
                                                leistung=wetter[wetter1[0].x1].wpbedarf*e3dc.speichergroesse/25;
                                                if (leistung < e3dc.WPmin)
                                                    leistung = e3dc.WPmin;
                                                
                                                // Um 100W erhöhen
                                                int j1 = (-iindxsoc[0] + wet.x1+96) / 96;
                                                if (leistung < av)   // Verdichterleistung erhöhen
                                                {
                                                    
                                                    if (
                                                        // aus PV bedienen
                                                        (
                                                         (wetter[wetter1[0].x1].hourly+wpbedarf<wetter[wetter1[0].x1].solar
                                                          && fhighsoc[j1]>0&&wet.status==2)
                                                         ||
                                                         // aus dem Netz bedienen
                                                         wet.status == 0
                                                         ||
                                                         // aus dem Speicher bedienen
                                                         (flowsoc[j1] > 0.1/e3dc.speichergroesse*25+e3dc.AWReserve
                                                          &&
                                                          wet.status == 1
                                                          &&
                                                          wetter[wetter1[0].x1].hh < itime[j1]
                                                          
                                                          )
                                                         )
                                                        )
                                                    {
                                                        // WP wird aus dem Speicher bedient
                                                        if (wet.status == 1
                                                            &&wetter[wetter1[0].x1].hourly+wpbedarf>wetter[wetter1[0].x1].solar)
                                                        {
                                                            
                                                            if (wpbedarf >0)
                                                                flowsoc[j1] = flowsoc[j1] - 0.1/e3dc.speichergroesse*25;
                                                            else
                                                                flowsoc[j1] = flowsoc[j1] - e3dc.WPmin/e3dc.speichergroesse*25;
                                                        }
                                                        if (wet.status == 2)
                                                        {
                                                            if (fhighsoc[j1]>0)
                                                                
                                                            {
                                                                if (wpbedarf >0)
                                                                    fhighsoc[j1] = fhighsoc[j1] - 0.1/e3dc.speichergroesse*25;
                                                                else
                                                                    fhighsoc[j1] = fhighsoc[j1] - e3dc.WPmin/e3dc.speichergroesse*25;
                                                            }
                                                            else
                                                                wetter1[0].status = 1;
                                                        }
                                                        
                                                        //                                                if (wet.status == 1)
                                                        //                                                    flowsoc[j1] = flowsoc[j1] + wetter[wetter1[0].x1].wpbedarf;
                                                        //                                                if (wet.status == 2&&fhighsoc[j1]>0&&wpbedarf>0)
                                                        //                                                    fhighsoc[j1] = fhighsoc[j1] + wetter[wetter1[0].x1].wpbedarf;
                                                        
                                                        if (wetter[wetter1[0].x1].wpbedarf > 0)
                                                        {
                                                            leistung = leistung + 0.1;
                                                            waermebedarf = waermebedarf + wetter[wetter1[0].x1].waerme/4;
                                                            // Kann die WP noch aus der Solarleistung abgedeckt werden?
                                                        }
                                                        
                                                        wetter[wetter1[0].x1].wpbedarf=leistung/e3dc.speichergroesse*25;
                                                        float f1 = wetter[wetter1[0].x1].wpbedarf;
                                                        float f2 = leistung*(wetter1[0].cop);
                                                        if (f2>wetter[wetter1[0].x1].waerme)
                                                            wetter[wetter1[0].x1].waerme = f2;
                                                        waermebedarf = waermebedarf - f2/4;
                                                        // Ermitteln des dyn. cop bei Minimum/Maximum bzw. der angeforderten Leistung
                                                        float cop1 = ((leistung + 0.1 - e3dc.WPmin)/diffleistung)*dyncop;
                                                        float cop = wetter[wetter1[0].x1].cop*(1+dyncop-cop1);
                                                        float waermepreis = wetter1[0].waermepreis;
                                                        if (waermepreis<0)
                                                            waermepreis = wetter1[0].waermepreis*cop/wetter1[0].cop;
                                                        else
                                                            waermepreis = wetter1[0].waermepreis*wetter1[0].cop/cop;
                                                        wetter1[0].waermepreis = waermepreis;
                                                        wetter[wetter1[0].x1].waermepreis = waermepreis;
                                                        wetter1[0].cop = cop;
                                                        
                                                        if (wet.status == 1)
                                                            flowsoc[j1] = flowsoc[j1] - wetter[wetter1[0].x1].wpbedarf;
                                                        
                                                        if (wetter[wetter1[0].x1].hourly+wpbedarf<wetter[wetter1[0].x1].solar
                                                            && wetter[wetter1[0].x1].hourly+f1>wetter[wetter1[0].x1].solar
                                                            && wetter1[0].status == 2 )
                                                        {
                                                            wetter1[0].waermepreis =                                           wetter1[0].waermepreis * 1.5;
                                                            wetter1[0].status = 1;
                                                        }
                                                    }
                                                    else
                                                    {
                                                        if (wet.status==2)
                                                        {
                                                            wetter1[0].waermepreis =                                           wetter1[0].waermepreis * 1.5;
                                                            wetter1[0].status = 1;
                                                        }
                                                        else
                                                            if (wet.status==1)
                                                            {
                                                                
                                                                wetter1[0].waermepreis = (w[wetter1[0].x1].pp*.1*(100+e3dc.AWMWSt)/100+e3dc.AWNebenkosten)/wet.cop;
                                                                wetter1[0].status = 0;
                                                            }
                                                    }
                                                    
                                                    // dyn. cop abhängig von der Leistung berechnen
                                                }
                                                
                                                else  // Einsatz BWWP und Heizstäbe
                                                {
                                                    if (wet.status==2)
                                                    {
                                                        if (fhighsoc[j1]>3/e3dc.speichergroesse*25)
                                                            fhighsoc[j1]=fhighsoc[j1]-1/e3dc.speichergroesse*25;
                                                        else
                                                            wetter1[0].status=1;
                                                    }
                                                    
                                                    if (wet.status==1)
                                                    {
                                                        if (flowsoc[j1]>3/e3dc.speichergroesse*25)
                                                            flowsoc[j1]=flowsoc[j1]-1/e3dc.speichergroesse*25;
                                                        else
                                                            wetter1[0].status=0;
                                                    }
                                                    if (wetter[wetter1[0].x1].wwwpbedarf==0)
                                                    {
                                                        static float bwwp = 0;
                                                        wetter[wetter1[0].x1].wwwpbedarf=0.5/e3dc.speichergroesse*25;
                                                        waermebedarf = waermebedarf - wetter[wetter1[0].x1].wwwpbedarf*e3dc.speichergroesse*.04;
                                                        bwwp = bwwp + wetter[wetter1[0].x1].wwwpbedarf*e3dc.speichergroesse*.04;
                                                        // Einsatz Heizstab Wärmepreis sind die Bezugskosten
/*
                                                        if (wetter[wetter1[0].x1].hourly+wetter[wetter1[0].x1].wpbedarf+1/e3dc.speichergroesse*25
                                                            >wetter[wetter1[0].x1].solar
                                                            ||
                                                            fhighsoc[j1]<1/e3dc.speichergroesse*25)
                                                            wetter1[0].waermepreis = (w[wetter1[0].x1].pp*.1*(100+e3dc.AWMWSt)/100+e3dc.AWNebenkosten);
                                                        else

                                                            wetter1[0].waermepreis = wetter1[0].waermepreis*wetter1[0].cop;
*/
                                                        //                                                    wetter1[0].waermepreis = (w[wetter1[0].x1].pp*.1*(100+e3dc.AWMWSt)/100+e3dc.AWNebenkosten);
                                                    }
                                                    else
                                                        if (wetter[wetter1[0].x1].wwwpbedarf>0) //heizstab
                                                        {
                                                            if (wetter1[0].waermepreis>=1000)
                                                                break; // keine heizreserven mehr
                                                            if (wetter[wetter1[0].x1].heizstabbedarf == 0)
                                                            {
                                                                wetter[wetter1[0].x1].heizstabbedarf =
                                                                1/e3dc.speichergroesse*25;
                                                                waermebedarf = (waermebedarf - 0.25);
                                                            }
                                                            else
                                                            {
                                                                wetter[wetter1[0].x1].heizstabbedarf =
                                                                wetter[wetter1[0].x1].heizstabbedarf + 3/e3dc.speichergroesse*25;
                                                                waermebedarf = (waermebedarf - 0.75);
                                                            }
                                                            if (wetter[wetter1[0].x1].hourly+wetter[wetter1[0].x1].wpbedarf+wetter[wetter1[0].x1].wwwpbedarf+wetter[wetter1[0].x1].heizstabbedarf
                                                                >wetter[wetter1[0].x1].solar
                                                                ||
                                                                fhighsoc[j1]<wetter[wetter1[0].x1].heizstabbedarf)
                                                                wetter1[0].waermepreis = (w[wetter1[0].x1].pp*.1*(100+e3dc.AWMWSt)/100+e3dc.AWNebenkosten);
                                                            else
                                                                wetter1[0].waermepreis = wetter1[0].waermepreis*wetter1[0].cop;

                                                                wetter1[0].waermepreis =
                                                                (w[wetter1[0].x1].pp*.1*(100+e3dc.AWMWSt)/100+e3dc.AWNebenkosten)+
                                                                wetter[wetter1[0].x1].heizstabbedarf*.1;

                                                            if (wetter[wetter1[0].x1].heizstabbedarf>=10/e3dc.speichergroesse*25)
                                                                wetter1[0].waermepreis = 1000;

                                                        }
                                                    
                                                }
                                            }
                                            
                                            
                                            if (leistung  > av)
                                            {
                                                // WP Leistung erreicht, jetzt gehts mit der BWWP weiter
                                                if (wetter[wetter1[0].x1].wwwpbedarf==0)
                                                {
                                                    if (wetter[wetter1[0].x1].hourly+wetter[wetter1[0].x1].wpbedarf+0.5/e3dc.speichergroesse*25
                                                        >wetter[wetter1[0].x1].solar)
                                                        wetter1[0].waermepreis = (w[wetter1[0].x1].pp*.1*(100+e3dc.AWMWSt)/100+e3dc.AWNebenkosten)/4;
                                                    else
                                                        
                                                        wetter1[0].waermepreis = wetter1[0].waermepreis*wetter1[0].cop/4;
                                                    wetter1[0].cop = 4;
                                                }
                                            }
                                            
                                            
                                        }
                                    }
                                    wetter1.clear();
                                }
                            }
                        }
                    }

                        
                    

 }

            }}
        {
            if (e3dc.openmeteo)
            {
                while (w.size()>0&&(w[0].hh+840)<=rawtime)  // eine Minute vorher löschen
                    w.erase(w.begin());
            }
            else
            {
                while (w.size()>0&&(w[0].hh+3540)<=rawtime)  // eine Minute vorher löschen
                    w.erase(w.begin());
            }
            if (e3dc.openmeteo)
                while (w.size()>0&&(w[0].hh+900<=rawtime))
                    w.erase(w.begin());

            int analyse = 0;
            float strompreis = 0;

            if (strlen(e3dc.analyse)>0)
            {
                w.clear();
                wetter.clear();
                e3dc.speichergroesse = 29.5;
                if (fp != NULL)
                fclose(fp);
                fp = NULL;
                fp = fopen(e3dc.analyse,"r");
                if (fp == NULL)
                    exit(99);
                watt_s ww;
                wetter_s we;
                float temp = 0;
                float hh = 0;
                char key[] = "Simulation";
                char key2[] = " Data \n";
                memset(&line, 0, sizeof(line));
                int ret = sizeof(line);
//                fgets(line,sizeof(line)-1,fp);
                while (fp != NULL)
                {
                    memset(&line, 0, sizeof(line));
                    if (fgets(line,sizeof(line),fp)==NULL)
                        break;
                    sscanf(line, " %[^ \t=]%*[ \t ] %*[\t ]%[^ \n]", var, value);
                    char s1[20],s2[20],s3[20],s4[20];
                    float f1,f2;
                    sscanf(line, "%s %s %f %s %s %f", s1,s2,&f1,s3,s4,&f2);
                    soc = f1;
                    notstromreserve = f2;
                    ret = (strcmp(key,var));
                    if (ret == 0)
                        break;
                }

                while (fp != NULL&&ret>=0)
                {
                    
                    ret =  fscanf(fp,"%f %f %f %f %f \n ",&hh,&strompreis,&soc,&we.wpbedarf,&we.solar);
                    if (ret==5){
                       

                        break;
                    }
                }
                while (fp != NULL)
                {
                    if (fgets(line,sizeof(line),fp)==NULL)
                        break;
                    ret = (strcmp(key2,line));
                    if (ret == 0)
                        break;
                }


                    while (fp != NULL&&ret>=0)
                    {

                    ret =  fscanf(fp,"%f %f %f %f %f %f \n ",&hh,&ww.pp,&we.hourly,&we.wpbedarf,&we.solar, &we.temp);
                    if (ret==6){
                        ww.hh = hh * 3600;
                        ww.pp=ww.pp*10;
                        we.hh = hh * 3600;
                        wetter.push_back(we);
                        w.push_back(ww);
                    }
                }
                if (fp != NULL)
                fclose(fp);

//                int CheckaWATTar(std::vector<watt_s> &w,std::vector<wetter_s> &wetter,int sunrise,int sunset,int sunriseWSW, float fSoC,float fmaxSoC,float fConsumption,float Diff,float aufschlag, float ladeleistung,int mode,float &fstrompreis, float reserve);
            }

            if (w.size() == 0) return;
            if (wetter.size() == 0) return;
            if (e3dc.debug) printf("NWS1\n");
//            if (e3dc.unload < 0) return;

            int    ret = CheckaWATTar(w ,wetter, soc, 99, -1.31, e3dc.AWDiff, e3dc.AWAufschlag,  e3dc.maximumLadeleistung*.9,0,strompreis,e3dc.AWReserve,notstromreserve,e3dc.speicherev/1000/e3dc.speichergroesse/4, e3dc.speichereta);

            memset(&line, 0, sizeof(line));
            fp = fopen("awattardebug.txt","w");
            sprintf(line,"awattarlog%i.out",ptm->tm_wday);
            struct stat stats;
             stat(line,&stats);
            time_t t = *(&stats.st_mtime);
             if ((rawtime-t)>(24*3600*5)) // nach 5 tagen überschreiben
                fp1 = fopen(line,"w");


         fp1 = fopen(line,"a");
         if (fp1 == NULL)
             fp1 = fopen(line,"w");

        if (e3dc.debug)
            printf("\n Simulation %zu %zu\n",w.size(),wetter.size());

    fprintf(fp,"\n Simulation soc %2.2f%% notstrom %2.2f%% \n\n",soc,notstromreserve);
//    fprintf(fp," Notstromreserve = %2.2f%% \n",notstromreserve);

            //         fprintf(fp,"\n Start %0.2f SoC\n",soc);
            float soc_alt = soc;;
//            soc = soc - e3dc.AWReserve; // Berücksichtigung der Reserve
            for (int j = 0;j<w.size();j++)
         {

             soc_alt = soc;
             if (w[j].hh > wetter[j].hh)
                 soc_alt = soc;

//             float x3 = wetter[j].hourly + wetter[j].wpbedarf + wetter[j].wwwpbedarf + wetter[j].heizstabbedarf - wetter[j].solar;;
             anforderung = (wetter[j].solar - wetter[j].hourly - wetter[j].wpbedarf - wetter[j].wwwpbedarf - wetter[j].heizstabbedarf);
             if ( anforderung> e3dc.maximumLadeleistung*.9/e3dc.speichergroesse/10)
                 anforderung = e3dc.maximumLadeleistung*.9/e3dc.speichergroesse/10;
             
             float preis = w[j].pp;
             float ladeleistung;
             float fsolar = wetter[j].solar;
             if (e3dc.openmeteo)
             {
                 if (fsolar > (e3dc.maximumLadeleistung*.9/e3dc.speichergroesse/10/4))
                     fsolar = (e3dc.maximumLadeleistung*.9/e3dc.speichergroesse/10/4);
                 ladeleistung = e3dc.maximumLadeleistung*.9/e3dc.speichergroesse/10/4;
             } else
             {
                 if (fsolar > e3dc.maximumLadeleistung*.9/e3dc.speichergroesse/10)
                     fsolar = e3dc.maximumLadeleistung*.9/e3dc.speichergroesse/10;
                 ladeleistung = e3dc.maximumLadeleistung*.9/e3dc.speichergroesse/10;
             }
             int ret = SimuWATTar(w ,wetter,j ,soc , anforderung, e3dc.AWDiff, e3dc.AWAufschlag, e3dc.AWReserve, notstromreserve, ladeleistung,e3dc.speicherev, e3dc.speichereta);
//             if (e3dc.debug) {printf("NWj%i %i %i %f %f \n",j,ret,e3dc.maximumLadeleistung,e3dc.speichergroesse,ladeleistung);
//                 sleep(1);}
             if (ret == 1)
             { 
                if (anforderung > ladeleistung)
                    soc = soc_alt + ladeleistung;
                else
                {
                    if (anforderung > 0)
                        soc = soc_alt + anforderung*e3dc.speichereta - e3dc.speicherev/1000/e3dc.speichergroesse;
                    else
                        soc = soc_alt + anforderung/e3dc.speichereta - e3dc.speicherev/1000/e3dc.speichergroesse;
                }
                if (soc > 100) soc = 100;
                if (soc < notstromreserve) soc = notstromreserve;
             }
             else
             if (ret == 0)
             {
                     float soc2 = soc_alt + fsolar - e3dc.speicherev/1000/e3dc.speichergroesse;
                     if (soc>soc2)
                         soc = soc2;
                     if (soc > 100) soc = 100;
                     if (soc < 0) soc = 0;
            }
             else
                     if (soc > 95) soc = 95;
             if (e3dc.openmeteo)
             {
                 if (e3dc.AWSimulation == 1)
                     sprintf(line,"%0.2f %0.3f %0.2f %0.2f %0.2f \n",float((w[j].hh%(24*3600))/3600.0),w[j].pp/10,soc_alt,(soc-soc_alt),wetter[j].solar);
                 else
                     sprintf(line,"%0.2f %0.3f %0.2f %0.2f \n",float((w[j].hh%(24*3600))/3600.0),w[j].pp/10,soc_alt,(soc-soc_alt));
             }
             else
             {
                 
                 if (e3dc.AWSimulation == 1)
                     sprintf(line,"%i %0.3f %0.2f %0.2f %0.2f \n",((w[j].hh%(24*3600))/3600),w[j].pp/10,soc_alt,(soc-soc_alt),wetter[j].solar);
                 else
                     sprintf(line,"%i %0.3f %0.2f %0.2f \n",((w[j].hh%(24*3600))/3600),w[j].pp/10,soc_alt,(soc-soc_alt));
                 
             }
             fprintf(fp,line);
             if (j< 2)
                 fprintf(fp1,line);
         }
            fprintf(fp,"\n\n Data \n\n");

            for (int j = 0;j<w.size();j++)
                if (e3dc.openmeteo)
                {
                    fprintf(fp,"%0.2f %0.3f %0.2f %0.2f %0.2f %0.2f",float((w[j].hh%(24*3600))/3600.0),w[j].pp/10,wetter[j].hourly,wetter[j].wpbedarf,wetter[j].solar,wetter[j].temp);
                    if (e3dc.WPWolf&&wetter[j].wwwpbedarf>0)
                        fprintf(fp," %0.1f",wetter[j].wwwpbedarf*e3dc.speichergroesse*.04);
                    if (e3dc.WPWolf&&wetter[j].heizstabbedarf>0)
                        fprintf(fp," %0.0f",wetter[j].heizstabbedarf*e3dc.speichergroesse*.04);

                    fprintf(fp,"\n");
                }
                else
                    fprintf(fp,"%i %0.3f %0.2f %0.2f %0.2f  \n",((w[j].hh%(24*3600))/3600),w[j].pp/10,wetter[j].hourly,wetter[j].wpbedarf,wetter[j].solar);

         fclose(fp);
         fclose(fp1);
if (e3dc.debug) printf("NWS2\n");
            if (strlen(e3dc.analyse)>0)
                e3dc.stop = 99;


            if (ptm->tm_min<2)
            {
                sprintf(line,"cp awattardebug.txt awattardebug.%i.txt",ptm->tm_hour);
                system(line);
            }

            if (e3dc.debug) printf("NWS3\n");

         


/*       Test zur Abfrage des Tesmota Relais

          FILE *fp;

          int WP_status,status;
          char path[PATH_MAX];


         fp = popen("mosquitto_sub -h 192.168.178.54 -t stat/tasmota/POWER4 -W 1 -C 1", "r");
         if (fp == NULL)
           
         WP_status = 0;
         while (fgets(path, PATH_MAX, fp) != NULL)
         if (strcmp(path, "ON"))
             WP_status = 1;


         status = pclose(fp);

*/
         
         
    
        }
    }
//     */
//    return 1;
};

