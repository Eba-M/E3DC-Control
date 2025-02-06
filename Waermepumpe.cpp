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

static float endpunkt = 45;  // Endpunkt bei -15°
static float absolutenull = 273; // absoluter Nullpunkt 0K
static float cop,wm,wp;
static time_t oldhour = 0;
static int oldwsize = -1;


// static float ftemp;
//mewp(w,wetter,fatemp,sunriseAt,e3dc_config);       // Ermitteln Wetterdaten

void mewp(std::vector<watt_s> &w,std::vector<wetter_s>&wetter,float &fatemp,float &cop, int sunrise, int sunset,e3dc_config_t &e3dc, float soc, int ireq_Heistab, float zuluft,float notstromreserve) {
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
        ((ptm->tm_min%15==0||oldhour==0||w.size()!=oldwsize)&&
        (
        (rawtime-oldhour)>=60||w.size()!=oldwsize)
        )
    {
        oldhour = rawtime;
        oldwsize = w.size();
//    /*{
        FILE * fp;
        FILE * fp1;
        char path [65000];
        char line[256];
        char value[25];
        char var[25];

        if (e3dc.debug) printf("NW1\n");

        if (e3dc.openmeteo)
        {
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
                fatemp = fatemp / x1;
                if (fp!=NULL) pclose(fp);
            }
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
        if (e3dc.debug) printf("NW4\n");

        {
            {
                if (e3dc.WPLeistung>0) {
                    // Ermitteln des Energiebedarfs für die Wärmepumpe
                    // Die Pelletsheizung wird eingesetzt, wenn die Leistung der WP nicht mehr
                    // ausreicht
                    float fbitemp = e3dc.WPHeizgrenze -
                    float(e3dc.WPLeistung/float(e3dc.WPHeizlast/(15+e3dc.WPHeizgrenze)));
                    // fbitemp Bivalenztemperator unter dieser Schwelle muss Pellets oder Heizstab
                    // eingesetzt werden EHZ
                    if (wetter.size()>0&&wetter.size()>0&&w[0].hh>wetter[0].hh)
                        wetter.erase(wetter.begin());

                    for (int x1=0;x1<w.size()&&x1<wetter.size();x1++)
                            if (wetter[x1].temp < 20&&e3dc.WPLeistung>0)

                            {
                                float f1=((-fusspunkt+endpunkt)/(e3dc.WPHeizgrenze+15))*(e3dc.WPHeizgrenze-wetter[x1].temp)+fusspunkt;
                                // Temperaturhub
                                float f2 = ((absolutenull+wetter[x1].temp)/(f1))*.6; // COP
                                if (cop <= 0) cop = f2;
                                // thermische Heizleistung
                                float f3 = ((e3dc.WPHeizgrenze-wetter[x1].temp))*(e3dc.WPHeizlast/(e3dc.WPHeizgrenze+15));
                                if (f3 < 0) f3=0; // zu warm keine Heizung
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
                                    wetter[x1].kosten = f4;
                                
                                if ((w.size()>0)&&x1<=w.size())
                                    if (wetter[x1].hh == w[x1].hh){
                                        // Überprüfen ob WP oder Pelletsheizung günstiger
                                        // Kosten = aktueller Strompreis / COP + EHZ
                                        float f7 =((w[x1].pp/10)*((100+e3dc.AWMWSt)/100)+e3dc.AWNebenkosten);
                                        float f8 =  f7*f4; // kosten = strompreis * Stromaufnahme
                                        float f6 =  f8/f5;  // kosten / Wärmebedarf
                                        float f9 = f7/f2;
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
                                            (e3dc.WPZWEPVon>0&&f9>e3dc.WPZWEPVon-0.2)) // Wärmepreis
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
                    float waermebedarf = (e3dc.WPHeizgrenze - fatemp)*24; // Heizgrade
                    waermebedarf = (e3dc.WPHeizlast / (e3dc.WPHeizgrenze + 15)) * waermebedarf;
                    // Heizlast bei -15°
                    float heizleistung = 0;
                    for (int x1=0;x1<w.size()&&x1<wetter.size()&&x1<96;x1++)
                        heizleistung = heizleistung + wetter[x1].waerme;
                    if (e3dc.WPWolf)
                    {
                        if (waermebedarf < 240)
                            // Verteilen des Wärmebedarfs auf die Zeiten der günstigsten Erzeugung, d.h. höchste Temperatur
                            int x2 = -1;
                        
                        float temp = -99;
                        std::vector<wetter1_s>wetter1; // Stundenwerte der Börsenstrompreise
                        wetter1_s wet;
                        for (int x1=0;x1<w.size()&&x1<wetter.size()&&x1<96;x1++)
                        {
                            wet.x1 = x1;
                            wet.temp = wetter[x1].temp;
                            wetter1.push_back(wet);
                        }
                        std::sort(wetter1.begin(), wetter1.end(), [](const wetter1_s& a, const wetter1_s& b) {
                            return a.temp > b.temp;});
                        wetter1.clear();
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

                if (fp != NULL)
                fclose(fp);
                fp = NULL;
                fp = fopen(e3dc.analyse,"r");
                if (fp == NULL)
                    return;
                watt_s ww;
                wetter_s we;
                float temp = 0;
                float hh = 0;
                char key[] = "Simulation";
                char key2[] = " Data \n";
                memset(&line, 0, sizeof(line));
                int ret = sizeof(line);
//                fgets(line,sizeof(line),fp);
                while (fp != NULL)
                {
                    memset(&line, 0, sizeof(line));
                    if (fgets(line,sizeof(line),fp)==NULL)
                        break;
                    sscanf(line, " %[^ \t=]%*[ \t ] %*[\t ]%[^ \n]", var, value);

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

            int    ret = CheckaWATTar(w ,wetter, soc, 99, -1.31, e3dc.AWDiff, e3dc.AWAufschlag,  e3dc.maximumLadeleistung*.9,0,strompreis,e3dc.AWReserve+notstromreserve);

            if (w.size() == 0) return;
            if (wetter.size() == 0) return;
            if (e3dc.debug) printf("NWS1\n");
            if (e3dc.unload < 0) return;
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

             anforderung = (wetter[j].solar - wetter[j].hourly - wetter[j].wpbedarf );
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
             int ret = SimuWATTar(w ,wetter,j ,soc , anforderung, e3dc.AWDiff, e3dc.AWAufschlag, e3dc.AWReserve+notstromreserve, ladeleistung);
//             if (e3dc.debug) {printf("NWj%i %i %i %f %f \n",j,ret,e3dc.maximumLadeleistung,e3dc.speichergroesse,ladeleistung);
//                 sleep(1);}
             if (ret == 1)
             { 
                if (anforderung > ladeleistung)
                    soc = soc_alt + ladeleistung;
                else
                {
                    soc = soc_alt + anforderung;
                }
                if (soc > 100) soc = 100;
                if (soc < notstromreserve) soc = notstromreserve;
             }
             else
             if (ret == 0)
             {
                     float soc2 = soc_alt + fsolar;
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
                    fprintf(fp,"%0.2f %0.3f %0.2f %0.2f %0.2f %0.2f  \n",float((w[j].hh%(24*3600))/3600.0),w[j].pp/10,wetter[j].hourly,wetter[j].wpbedarf,wetter[j].solar,wetter[j].temp);
                else
                    fprintf(fp,"%i %0.3f %0.2f %0.2f %0.2f  \n",((w[j].hh%(24*3600))/3600),w[j].pp/10,wetter[j].hourly,wetter[j].wpbedarf,wetter[j].solar);

         fclose(fp);
         fclose(fp1);
            if (e3dc.debug) printf("NWS2\n");

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

