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
        for(int j=0;j<w.size()&&j<stunden;j++)
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

void mewp(std::vector<watt_s> &w,std::vector<wetter_s>&wetter,float &fatemp,float &cop, int sunrise, int sunset,e3dc_config_t &e3dc, float soc, int ireq_Heistab, float zuluft) {
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
        cop = ((absolutenull+zuluft)/(((-fusspunkt+endpunkt)/(e3dc.WPHeizgrenze+15))*(e3dc.WPHeizgrenze-zuluft)+fusspunkt))*.44;

// Jede Stunde oder wenn neue Börsenstrompreise verfügbar sind auch früher
    if ((ptm->tm_min%15==0||oldhour==0)&&
        (
        (rawtime-oldhour)>=3600||e3dc.openmeteo&&(rawtime-oldhour)>=900||w.size()>oldwsize)
        )
    {
        oldhour = rawtime;
        oldwsize = w.size();
//    /*{
        FILE * fp;
        char line[256];
        char path [65000];
        char value[25];
        char var[25];

        if (e3dc.openmeteo)
        {
            sprintf(line,"curl -X GET 'https://api.open-meteo.com/v1/forecast?latitude=%f&longitude=%f&minutely_15=temperature_2m&timeformat=unixtime&forecast_minutely_15=192'",e3dc.hoehe,e3dc.laenge);
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
                item1 = item1->child;
                item2 = item2->child;
                int x1 = 0;
                wetter.clear();
                fatemp = 0;
                while (item1!=NULL)
                {
                    we.hh = item1->valueint;
                    we.temp = item2->valuedouble;
                    wetter.push_back(we);
                    fatemp = fatemp + we.temp;
                    item1 = item1->next;
                    item2 = item2->next;
                    x1++;

                }
                fatemp = fatemp / x1;

            }
        }
        else
        if (strlen(e3dc.openweathermap)>0)
        {
            
            sprintf(line,"curl -X GET 'https://api.openweathermap.org/data/2.5/onecall?lat=%0.6f&lon=%0.6f&appid=%s&exclude=(current,minutely,alerts)&units=metric' | jq .hourly| jq '.[]' | jq '.dt, .temp, .clouds, .uvi'>wetter.out",e3dc.hoehe,e3dc.laenge,e3dc.openweathermap);
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
        {
            {
                if (e3dc.WPLeistung>0) {
                    // Ermitteln des Energiebedarfs für die Wärmepumpe
                    // Die Pelletsheizung wird eingesetzt, wenn die Leistung der WP nicht mehr
                    // ausreicht
                    int fbitemp = e3dc.WPHeizgrenze-
                    e3dc.WPHeizlast/(15+e3dc.WPHeizgrenze)*e3dc.WPLeistung;
                    
                    for (int x1=0;x1<w.size()&&x1<wetter.size();x1++)
                            if (wetter[x1].temp < 20&&e3dc.WPLeistung>0)

                    {
                        float f1=((-fusspunkt+endpunkt)/(e3dc.WPHeizgrenze+15))*(e3dc.WPHeizgrenze-wetter[x1].temp)+fusspunkt;
                        // Temperaturhub
                                                float f2 = ((absolutenull+wetter[x1].temp)/(f1))*.45; // COP
                                                if (cop < 0) cop = f2;
                                                // thermische Heizleistung
                                                float f3 = (e3dc.WPHeizgrenze-wetter[x1].temp)*(e3dc.WPHeizlast/(e3dc.WPHeizgrenze+15));
                                                float f4 = 0;
                                                float f5 = f3; // angeförderte Heizleistung
                                                // Heizstab verwenden? angeforderte Heizleistung > Nennleistung WP
                                                if (f3 > e3dc.WPLeistung) {
                                                    f4 = f3 - e3dc.WPLeistung;
                                                    f3 = e3dc.WPLeistung;
                                                }
                                                // Heizleistung WP unter Nennwert => mehr Heizstab
                                                if (f3/f2>e3dc.WPmax)
                                                    f4 = f4 + f3 - f2*e3dc.WPmax + e3dc.WPmax;
                                                else
                                                    f4 = f4 + f3/f2; // benötigte elektrische Leistung;
                        if (e3dc.openmeteo)
                        {
                            wetter[x1].kosten = f4/e3dc.speichergroesse*100/4;
                            while (w.size()>0&&(w[0].hh+900<=rawtime))
                                w.erase(w.begin());
                            while (wetter.size()>0&&(wetter[0].hh+900<=rawtime))
                                wetter.erase(wetter.begin());
                        }
                        else
                            wetter[x1].kosten = f4/e3dc.speichergroesse*100;

                            if ((w.size()>0)&&x1<=w.size())
                                                    if (wetter[x1].hh == w[x1].hh){
                    // Überprüfen ob WP oder Pelletsheizung günstiger
                    // Kosten = aktueller Strompreis / COP
                    float f7 =((w[x1].pp/10)*((100+e3dc.AWMWSt)/100)+e3dc.AWNebenkosten);
                    float f8 =  f7*f4/f5;
                    float f6 =  f7/f2;

                        w[x1].wpbedarf = wetter[x1].kosten;
                        if (e3dc.WPZWE>wetter[x1].temp)
                    // Pelletskessel übernimmt und die WP läuft auf Minimum weiter
                            if (e3dc.openmeteo)
                                w[x1].wpbedarf = e3dc.WPmin/e3dc.speichergroesse*100/4;
                            else
                                w[x1].wpbedarf = e3dc.WPmin/e3dc.speichergroesse*100;

                                // wenn der Wärmepreis der WP günstiger ist als Pellets
                        if (f6<e3dc.WPZWEPVon)
                            if (e3dc.openmeteo)
                                w[x1].wpbedarf = (f3/f2) /e3dc.speichergroesse*100/4;
                            else
                                w[x1].wpbedarf = (f3/f2) /e3dc.speichergroesse*100;
                                                    }
                                            }
                     

                    // Aus der ermittelte Durchnittstemperatur geht der Wärmebedarf und damit
                    // die Laufdauer der Wärmepumpe vor
                    // Wenn die Laufzeit < 22h ist, beginnt der WP-Start 2h nach Sonnenaufgang
                    // bis dahin werden die Preise aus w.wpbedarf auf 0 gesetzt
                    float waermebedarf = (e3dc.WPHeizgrenze - fatemp)*24; // Heizgrade
                    waermebedarf = (e3dc.WPHeizlast / (e3dc.WPHeizgrenze + 15)) * waermebedarf;
                    // Heizlast bei -15°
/*
                    int heizdauer = (waermebedarf / e3dc.WPLeistung)*60;
                    heizbegin = sunrise + 120;
                    heizende = (heizbegin + heizdauer)%(24*60);
                    int w1 = 0;
                    int x1 = 0;
                    if (wetter.size()>0&&w.size()>0)
                        while (wetter[w1].hh < w[0].hh&&x1<wetter.size()) w1++;
                    
                    if (heizdauer < 12*60)
                        for (int j=0;j<w.size();j++)
                        {
                            int minuten = (w[j].hh%(24*3600))/60 ;
                            if (wetter[j].hh == w[j].hh)
                                if (
                                    (heizbegin > heizende)
                                    &&
                                    ((w[j].hh%(24*3600))/60 < heizbegin)
                                    &&
                                    ((w[j].hh%(24*3600))/60 > heizende)
                                    )
                                    
                                    w[j].wpbedarf = 0;
                            if
                                (heizbegin < heizende)
                                
                                if
                                    (((w[j].hh%(24*3600))/60 < heizbegin)
                                     ||
                                     ((w[j].hh%(24*3600))/60 > heizende)
                                     )
                                    
                                    w[j].wpbedarf = 0;
                            
                            if (w[j].wpbedarf > 0) // Leistungsbedarf auf max. korregieren
                            {
                                float f1 = (endpunkt - fusspunkt)/e3dc.WPHeizlast*(e3dc.WPHeizgrenze-wetter[w1+j].temp)+fusspunkt; // Temperaturhub
                                float f2 = ((absolutenull+wetter[w1+j].temp)/f1)*.5; // COP
                                float f3 = (e3dc.WPHeizgrenze-wetter[w1+j].temp)*e3dc.WPHeizlast/30;
                                //                     float f4 = f3/f2; // benötigte elektrische Leistung;
                                float f4 = e3dc.WPLeistung/f2; // benötigte elektrische Leistung;
                                float f5 = w[j].wpbedarf;
                                f5 = f4/e3dc.speichergroesse*100;
                                wetter[w1+j].kosten = f5;
                                w[j].wpbedarf = f5;
                            }
                            
                        }
*/
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

         fp = fopen("awattardebug.out","w");
         for (int j = 0;j<w.size();j++)
if (e3dc.openmeteo)
    fprintf(fp,"%0.2f %0.2f %0.2f %0.2f %0.2f  \n",float((w[j].hh%(24*3600))/3600.0),w[j].pp,w[j].hourly,w[j].wpbedarf,w[j].solar);
else
    fprintf(fp,"%i %0.2f %0.2f %0.2f %0.2f  \n",((w[j].hh%(24*3600))/3600),w[j].pp,w[j].hourly,w[j].wpbedarf,w[j].solar);

    fprintf(fp,"\n Simulation \n\n");
//         fprintf(fp,"\n Start %0.2f SoC\n",soc);
         float soc_alt;
         for (int j = 0;j<w.size();j++)
         {
             soc_alt = soc;
             anforderung = (w[j].solar - w[j].hourly - w[j].wpbedarf );
             if ( anforderung> e3dc.maximumLadeleistung*.9/e3dc.speichergroesse/10)
                 anforderung = e3dc.maximumLadeleistung*.9/e3dc.speichergroesse/10;
             
             float preis = w[j].pp;
             int ret = SimuWATTar(w ,j ,soc , anforderung, e3dc.AWDiff, e3dc.AWAufschlag, e3dc.maximumLadeleistung*.9/e3dc.speichergroesse/10);
             float fsolar = w[j].solar;
             if (fsolar > e3dc.maximumLadeleistung*.9/e3dc.speichergroesse/10)
                 fsolar = e3dc.maximumLadeleistung*.9/e3dc.speichergroesse/10;
             if (ret == 0)
             { if ((w[j].solar - w[j].hourly - w[j].wpbedarf ) > 0)
                 soc = soc - w[j].hourly - w[j].wpbedarf + fsolar;
                 if (soc > 100) soc = 100;
             } else
                 if (ret == 1) {
                     float soc2 = soc_alt - w[j].hourly - w[j].wpbedarf + fsolar;
                     if (soc<soc2)
                         soc = soc2;
                     if (soc > 100) soc = 100;
                     if (soc < 0) soc = 0;
                 }
             if (e3dc.openmeteo)
             {
                 if (e3dc.AWSimulation == 1)
                     fprintf(fp,"%0.2f %0.2f %0.2f %0.2f %0.2f \n",float((w[j].hh%(24*3600))/3600.0),w[j].pp,soc_alt,(soc-soc_alt),w[j].solar);
                 else
                     fprintf(fp,"%0.2f %0.2f %0.2f %0.2f \n",float((w[j].hh%(24*3600))/3600.0),w[j].pp,soc_alt,(soc-soc_alt));
             }
             else
             {
                 
                 if (e3dc.AWSimulation == 1)
                     fprintf(fp,"%i %0.2f %0.2f %0.2f %0.2f \n",((w[j].hh%(24*3600))/3600),w[j].pp,soc_alt,(soc-soc_alt),w[j].solar);
                 else
                     fprintf(fp,"%i %0.2f %0.2f %0.2f \n",((w[j].hh%(24*3600))/3600),w[j].pp,soc_alt,(soc-soc_alt));
                 
             }
         }
         fclose(fp);

             
         


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

