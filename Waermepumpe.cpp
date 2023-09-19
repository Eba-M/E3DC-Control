//
//  Waermepumpe.cpp
//  E3DC-V1
//
//  Created by Eberhard Mayer on 11.09.23.
//


#include "Waermepumpe.hpp"
#include "awattar.hpp"
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <array>
#include <algorithm>
#include <vector>

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



static std::vector<wetter_s>wetter; // Stundenwerte der Börsenstrompreise
static wetter_s we;
static float fusspunkt = 28; // Fusspunkt bei 15°
static float heizlast = 30;  // Heizlast  bei -15°
static float endpunkt = 40;  // Endpunkt bei -15°
static float absolutenull = 273; // absoluter Nullpunkt 0K
static float cop,wm,wp;
static int oldhour = -1;
// static float ftemp;
void mewp(float &fatemp) {
    time_t rawtime;
    struct tm * ptm;
    time(&rawtime);
    ptm = gmtime (&rawtime);

    if (ptm->tm_hour!=oldhour)
    {
        oldhour = ptm->tm_hour;
//    /*{
     FILE * fp;
     char line[256];


     
     sprintf(line,"curl -X GET 'https://api.openweathermap.org/data/2.5/onecall?lat=50.252526&lon=10.308570&appid=615b8016556d12f6b2f1ed40f5ab0eee&exclude=(current,minutely,alerts)&units=metric' | jq .hourly| jq '.[]' | jq '.dt, .temp, .clouds, .uvi'>wetter.out");
     int res = system(line);
     fp = fopen("wetter.out","r");
     
     if(fp)
     {
         wetter.clear();
         fatemp = 0;
         
         while (fgets(line, sizeof(line), fp))
         {
             
             we.hh = atol(line);
             if (fgets(line, sizeof(line), fp))
             {
                 we.temp = atof(line);
                 fatemp = fatemp + we.temp;
                 if (we.temp < 15)
                 {
                     float f1 = (endpunkt - fusspunkt)/30*(15-we.temp)+fusspunkt; // Temperaturhub
                     float f2 = ((absolutenull+we.temp)/f1)*.5; // COP
                     float f3 = (15-we.temp)*heizlast/30;
                     float f4 = f3/f2; // benötigte elektrische Leistung;
                 }
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
         }
         
         fclose(fp);
         fatemp = fatemp / 48;
         
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

