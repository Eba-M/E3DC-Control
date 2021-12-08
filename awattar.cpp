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

static time_t tm_Wallbox_dt = 0;
static watt_s ww;
static int oldhour = 24; // zeitstempel Wallbox Steurungsdatei;
int Diff = 100;           // Differenz zwischen niedrigsten und höchsten Börsenwert zum der Speicher nachgeladen werden soll.
int hwert = 5; // Es wird angenommen, das pro Stunde dieser Wert aus dem Speicher entnommen wird.
static std::vector<watt_s> w;
static watt_s high;
static watt_s high2;
static watt_s low;
static watt_s low2;
static int l1 = 0, l2 = 0, h1 = 0, h2 = 0;

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
        return false;
    else
        {tm_Wallbox_dt = tm_dt;
            return true;}
}

int Highprice(int ab,int bis,float preis)    // Anzahle Einträge mit hohen Preisen von low + Diff
{
    high = w[ab];
    low = w[ab];
    h1 = ab;
    l1 = ab;
    int x1 = 0;
    for (int j = ab; j <= bis; j++ )
    {
// Suchen nach Hoch und Tiefs
        if (w[j].pp > preis) {
            x1++;
        }
    }
    return x1;
}

void SucheHT(int ab,int bis)  // ab = Index bis zeitangabe in Minuten
{
    time_t  rawtime;
    struct tm * ptm;
    time(&rawtime);
    ptm = gmtime (&rawtime);
    ptm->tm_hour = bis/60;
    if (ptm->tm_hour>24) ptm->tm_hour = ptm->tm_hour-24;
    rawtime = mktime(ptm);
    if (bis/60 > 24)
    rawtime = rawtime + 24*3600;
    ptm = gmtime (&rawtime);

    high = w[ab];
    low = w[ab];
    h1 = ab;
    l1 = ab;
    time_t  tm_dt;
    int zeit;
    for (int j = ab; ((j < w.size())&&(w[j].hh<rawtime)); j++ )
    {
// Suchen nach Hoch und Tiefs
        zeit = w[j].hh%(24*3600)/60;
        if (w[j].pp > high.pp) {
            high = w[j];
            h1 = j;
        } else
          if  (w[j].pp < low.pp) {
              low = w[j];
              l1 = j;
          }
    }
    return;
}

int SuchePos(int ab,int bis)  // ab = Index bis zeitangabe in Minuten Suchen nach dem Zeitpunkt
{
    time_t  rawtime;
    struct tm * ptm;
    time(&rawtime);
    ptm = gmtime (&rawtime);
    ptm->tm_hour = bis/60;
    if (ptm->tm_hour>24) ptm->tm_hour = ptm->tm_hour-24;
    rawtime = mktime(ptm);
    if (bis/60 > 24)
    rawtime = rawtime + 24*3600;
    ptm = gmtime (&rawtime);
    time_t  tm_dt;
    int zeit, ret;
    for (int j = ab; ((j < w.size())&&(w[j].hh<rawtime)); j++ )
    {
// Suchen nach Hoch und Tiefs
        zeit = w[j].hh%(24*3600)/60;
        ret = j;
    }
    return ret;
}

int CheckaWATTar(int sunrise,int sunset,float fSoC,float fConsumption)

// Returncode 0 = keine Aktion, 1 Batterieentladen stoppen 2 Batterie mit Netzstrom laden
{
// Ermitteln Stundenanzahl
// Analyseergebnisse in die Datei schreiben

    time_t  rawtime;
    time(&rawtime);
    int x2;
    int Minuten = rawtime%(24*3600)/60;
    
    if (Minuten <= sunrise)
    {
        SucheHT(0,sunrise);
        x2 = SuchePos(l1,sunrise+120);
        x2 = Highprice(l1,x2,w[l1].pp+100);
        if (fSoC < x2*fConsumption)  
            return 2; else
        return false;
    }
    if ((Minuten> sunrise)&&(Minuten <= sunset))
    {
        return false;
    }
    if (Minuten > sunset)
    {
        SucheHT(0,sunrise+24*60); // sunrise nächster Tag
        if (h1 > l1)   // ist noch ein h1 vor dem low?
        
        SucheHT(0,w[l1].hh%(24*3600)/60);
        x2 = Highprice(0,l1,w[l1].pp+100);
        if (x2 == 0)                        // Es muss nichts mehr ausgespeichert werden
        {SucheHT(0,sunrise+120);            // l1 = geringster   h1 = höchster preis
        x2 = SuchePos(l1,sunrise+120);
        x2 = Highprice(l1,x2,w[l1].pp+100);
        }                                // Nachladen aus dem Netz erforderlich
        if (fSoC < x2*fConsumption)      // Stunden mit hohen Börsenpreisen, Nachladen wenn SoC zu niedrig
            return 2; else
        return 0;
    }
    return 0;
}


            
            
            
void aWATTar(std::vector<watt_s> &ch)
/*
 
 Diese Routine soll beim Programmstart und bei Änderungen in der
 Datei e3dc.wallbox.txt
 und dann 1x Stunde ausgeführt werden.
 
 
 */
{
//    std::vector<watt_s> ch;  //charge hour
    
int ladedauer = 4;
    time_t rawtime;
    struct tm * ptm;
    double_t pp;
    FILE * fp;
    char line[256];
    time(&rawtime);
    ptm = gmtime (&rawtime);
    if ((ptm->tm_hour==oldhour))
        return; //   Do nothing
    
    oldhour = ptm->tm_hour;

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
    if(!fp) return;
    w.clear();

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
    if (w.size() == 0)
    return;
    
    
  

    high = w[0];
    low = w[0];
    int x1 = 0;
//    Suchraum festlegen
//    Wenn der ersten
  
    for (int j = 0; j < w.size(); j++ )
    {
// Suchen nach Hoch und Tiefs
        if (w[j].pp > high.pp) {
            high = w[j];
            h1 = j;
        } else
          if  (w[j].pp < low.pp) {
              low = w[j];
              l1 = j;
          }
        
    }
// die gewünschten Ladezeiten werden ermittelt
// Beginn mit der aktuellen Uhrzeit bis nächsten Tag 7Uhr
    if (not(CheckWallbox()))
        return;
    ptm = gmtime ( &w[0].hh);
    int k = ptm->tm_hour;
    if (k > 7) k = 24-k+7;
    else k = 7;   // Es wird nur bis 7 Uhr nächsten Tag berücksichtigt
    if (k >w.size()) k = w.size();
    // ersten wert hinzufügen
    
    if (k>0&&ladedauer>0){
        if (low.pp < low2.pp)
            ww = low; else
            ww = low2;
        pp = ww.pp;
    ch.push_back(ww);
    for (int l = 1;((l < k)&&(l < ladedauer)); l++)
    {
        ww.pp = 1000;
        for (int j = 0; j < k; j++ )
        {
            if ((w[j].pp>pp)&&(w[j].pp<ww.pp))
            {
                ww =  w[j];
            }
        }
        ch.push_back(ww);

        

    }
    fp = fopen("e3dc.wallbox.txt","w");
    fprintf(fp,"%i\n",ladedauer);
    for (int j = 0; j < ch.size(); j++ ){
        k = (ch[j].hh% (24*3600)/3600);
        fprintf(fp,"%i %.2f; ",k,ch[j].pp);
    }
    fprintf(fp,"\n");
    fclose(fp);
    CheckWallbox();
    }}
