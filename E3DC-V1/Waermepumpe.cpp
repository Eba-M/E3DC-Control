//
//  Waermepumpe.cpp
//  E3DC-V1
//
//  Created by Eberhard Mayer on 11.09.23.
//

#include "Waermepumpe.hpp"
//#include "awattar.hpp"
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
static wetter_s ww;
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
             
             ww.hh = atol(line);
             if (fgets(line, sizeof(line), fp))
             {
                 ww.temp = atof(line);
                 fatemp = fatemp + ww.temp;
             } else break;
             if (fgets(line, sizeof(line), fp))
             {
                 ww.sky = atoi(line);
             } else break;
             if (fgets(line, sizeof(line), fp))
             {
                 ww.uvi = atof(line);
             } else break;
             
             wetter.push_back(ww);
         }
         
         fclose(fp);
         fatemp = fatemp / 48;
         
         
         
        }
    }
//     */
//    return 1;
};

