//
//  aWATTar.cpp
//  
//
//  Created by Eberhard Mayer on 26.11.21.
//  Copyright © 2021 Eberhard Mayer. All rights reserved.
//  

#include "awattar.hpp"
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <array>
#include <vector>

//typedef struct {int hh; float pp;}watt_s;

static time_t tm_Wallbox_dt; // zeitstempel Wallbox Steurungsdatei;
bool CheckWallbox()
/*
Mit dieser Funktion wird überprüft, ob die Wallbox für das Laden zu
aWATTar -Tarifen freigeschaltet werden muss.
Wenn über den Webserver eine neue Ladedaueränderung erkannt wurde
oder jede Stunde wird aWATTar aufgerufen, um die neuen aWATTar preise zu verarbeiten.
 */
{
    struct stat stats;
    time_t  tm_dt;
     stat("e3dc.wallbox.txt",&stats);
     tm_dt = *(&stats.st_mtime);
    if (tm_dt==tm_Wallbox_dt)
        return false; else return true;
};

void aWATTar(std::vector<watt_s> &ch)

{
//    std::vector<watt_s> ch;  //charge hour
    std::vector<watt_s> w;
    int ladedauer = 4;
    watt_s ww;
    
    struct tm * ptm;
    double_t pp;
    
    FILE * fp;
    char line[256];


//    system("curl -X GET 'https://api.awattar.de/v1/marketdata'| jq .data| jq '.[]' | jq '.start_timestamp%86400000/3600000, .marketprice'> awattar.out");
// es wird der orginale Zeitstempel übernommen um den Ablauf des Zeitstempels zu erkennen
    system("curl -X GET 'https://api.awattar.de/v1/marketdata'| jq .data| jq '.[]' | jq '.start_timestamp/1000, .marketprice'> awattar.out");

    system ("pwd");
    fp = fopen("e3dc.wallbox.txt","r");
    if (fp)
    {
        (fgets(line, sizeof(line), fp)); // Nur eine Zeile mit dem Angabe der Ladedauer lesen
        ladedauer = atoi(line);
        fclose(fp);
    };
    fp = fopen("awattar.out","r");
    if (fp)
    while (fgets(line, sizeof(line), fp)) {

        ww.hh = atol(line);
        ptm = gmtime ( &ww.hh);
//        h.push_back(hh);
        if (fgets(line, sizeof(line), fp)) {
            ww.pp = atof(line);
            w.push_back(ww);
        } else break;
    }
    fclose(fp);
    watt_s high = w[0];
    watt_s high2 = w[0];
    watt_s low = w[0];
    watt_s low2 = w[0];


    for (int j = 1; j < w.size(); j++ )
    {
// Suchen nach Hoch und Tiefs und zwar a) bis nächsten 12h b) dannach
        if (w[j].pp > high.pp) {
            high = w[j];
        } else
          if  (w[j].pp < low.pp) {
              low = w[j];
              high2 = w[j];
        }
         
        if (w[j].pp > high2.pp) {
            high2 = w[j];
            low2 = w[j];
        } else
          if  (w[j].pp < low2.pp) {
                low2 = w[j];
                high2 = w[j];
        }

    }
// die gewünschten Ladezeiten werden ermittelt
// Beginn mit der aktuellen Uhrzeit bis nächsten Tag 7Uhr
    ptm = gmtime ( &w[0].hh);
    int k = ptm->tm_hour;
    if (k > 7) k = 24-k+7;
    else k = 7;   // Es wird nur bis 7 Uhr nächsten Tag berücksichtigt
    if (k >w.size()) k = w.size();
    // ersten wert hinzufügen
    
    if (k>0&&ladedauer>0){
    ww = low;
    ch.push_back(ww);
    for (int l = 1;((l < k)&&(l < ladedauer)); l++)
    {
        pp = low.pp;
        ww = high;
        for (int j = 0; j < k; j++ )
        {
            if ((w[j].pp>pp)&&(w[j].pp<ww.pp))
            {
                ww =  w[j];
            }
        }
        ch.push_back(ww);
        low = ww;
        

    }}
    w.clear();
    fp = fopen("e3dc.wallbox.txt","w");
    fprintf(fp,"%i\n",ladedauer);
    for (int j = 0; j < ch.size(); j++ ){
        k = (ch[j].hh% (24*3600)/3600);
        fprintf(fp,"%i %.2f; ",k,ch[j].pp);
    }
    fprintf(fp,"\n");
    fclose(fp);
};
