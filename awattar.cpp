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
#include <time.h>
#include <array>
#include <algorithm>
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

int Highprice(int ab,int bis,float preis)    // Anzahle Einträge mit > preis
{                                            // l1 = erste position h1 = letzte Position
    int x1 = 0;
    for (int j = ab; j <= bis; j++ )
    {
// Suchen nach Hoch und Tiefs
        if (w[j].pp > preis) x1++;
    }
    return x1;
}
int Lowprice(int ab,int bis,float preis)    // Anzahle Einträge mit < Preisf
{                                            // l1 = erste position h1 = letzte Position
    int x1 = 0;
    for (int j = ab; j <= bis; j++ )
    {
// Suchen nach Hoch und Tiefs
        if (w[j].pp < preis) x1++;
    }
    return x1;
}

void SucheHT(int ab,long bis)  // ab = Index bis zeitangabe in Minuten oder Sekunden seit 1970
{
    time_t  rawtime;
    struct tm * ptm;
    if (bis < 48*60) {// bis kann entweder Tagesminutensumme oder Sekunden set 1970 sein
    time(&rawtime);
    ptm = gmtime (&rawtime);
    ptm->tm_hour = bis/60;
    if (ptm->tm_hour>24) ptm->tm_hour = ptm->tm_hour-24;
    rawtime = mktime(ptm);
    if (bis/60 > 24)
    rawtime = rawtime + 24*3600;
//    ptm = gmtime (&rawtime);
    } else rawtime = bis;
    high = w[ab];
    low = w[ab];
    h1 = ab;
    l1 = ab;
    time_t  tm_dt;
    int zeit;
    for (int j = ab; ((j < w.size())&&(w[j].hh<=rawtime)); j++ )
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
    time(&rawtime);
    ptm = gmtime (&rawtime);

    return;
}
bool SucheDiff(int ab, float aufschlag,int Diff)  // ab = Index bis Diff zwischen high und low erreicht
{
    high = w[ab];
    low = w[ab];
    h1 = ab;
    l1 = ab;
 
    for (int j = ab; (j < w.size()); j++ )
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
        float erg = (low.pp*(aufschlag-1) + Diff);
        if ((high.pp - low.pp) > (low.pp*(aufschlag-1) + Diff))
        return true;
    }

    return false;
}


int SuchePos(int bis)  // ab = Index bis zeitangabe in Minuten Suchen nach dem Zeitpunkt oder Sekunden nach 1970
{
    time_t  rawtime;
    struct tm * ptm;
    time(&rawtime);
    ptm = gmtime (&rawtime);
    ptm->tm_hour = bis/60;
    if (bis <= 48*60)
    {
    if (ptm->tm_hour>24) ptm->tm_hour = ptm->tm_hour-24;
    rawtime = mktime(ptm);
    if (bis/60 > 24)
    rawtime = rawtime + 24*3600;
    ptm = gmtime (&rawtime);
    }
    else rawtime = bis;
    int zeit, ret=0;
    for (int j = 0; ((j < w.size())&&(w[j].hh<rawtime)); j++ )
    {
// Suchen nach Hoch und Tiefs
        zeit = w[j].hh%(24*3600)/60;
        ret = j;
    }
    time(&rawtime);
    ptm = gmtime (&rawtime);
    return ret;
}

int CheckaWATTar(int sunrise,int sunset,float fSoC,float fmaxSoC,float fConsumption,float Diff,float aufschlag, float ladeleistung) // fConsumption Verbrauch in % SoC Differenz Laden/Endladen

// Returncode 0 = keine Aktion, 1 Batterieentladen stoppen 2 Batterie mit Netzstrom laden
{
// Ermitteln Stundenanzahl
// Analyseergebnisse in die Datei schreiben
    static float lowpp;
    time_t  rawtime;
    time(&rawtime);
    int x1,x2,x3;
    int Minuten = rawtime%(24*3600)/60;

//    return 2;  // Zu testzwecken  dann 9 Minuten Netzladebetrieb
/*
    if (Minuten%60<1)
        return 1;  else // Zu testzwecken erst eine Minute Entladen erlauben
    {
        if (Minuten%60<6)
            return 2;  // Zu testzwecken  dann 9 Minuten Netzladebetrieb
    }
 */
    if (w.size() == 0) return 0; // Preisvector ist leer
    int taglaenge = sunset-sunrise;
    int tagoffset = 12*60-taglaenge;
    tagoffset = tagoffset/2;
    if (tagoffset < 0) tagoffset = 0;
    
// testroutine neue auswertung
    if (low2.pp == 0)
    {
        if (SucheDiff(0, aufschlag,Diff))
            low2 = w[l1];
        else
            low2.pp = (Diff+Diff*aufschlag);
    } // ist low vorbelegt?


    if (SucheDiff(0, aufschlag,Diff))
    {
        if (h1>l1)       // erst kommt ein low dann ein high, überprüfen ob zum low geladen werden soll
        {
            int lw = l1;
            do
            if (not (SucheDiff(h1, aufschlag,Diff))) break; // suche low nach einem high
            while (h1 > l1);
// Überprüfen ob Entladen werden kann
            x1 = Lowprice(0, h1, w[0].pp);   // bis zum high suchen
            x2 = Highprice(0,l1,w[0].pp*aufschlag+Diff);  // Preisspitzen, es muss mindestens eine vorliegen
                                            // Nachladen aus dem Netz erforderlich
//            if (((fSoC < (x2*fConsumption+5))&&((l1==0)||(x2*fConsumption-fSoC)>x1*23))&&(fSoC<fmaxSoC-1))      // Stunden mit hohen Börsenpreisen, Nachladen wenn SoC zu niedrig
            if ((x2>0)&&        // es gibt mind. einen Wert mit dem nötigen aufschlag+Diff
                (((fSoC < (fmaxSoC-1))&&((lw==0)||(fmaxSoC-1-fSoC)>x1*ladeleistung*.9))&&(fSoC<fmaxSoC-1)))      // Stunden mit hohen Börsenpreisen, Nachladen wenn SoC zu niedrig
            {   low2 = w[0];
                return 2;}
            else if (x2>0) return 0; // Nicht entladen da die Preisdifferenz zur Spitze zu groß
        } else
            do
            if (not (SucheDiff(l1, aufschlag,Diff))) break; // suche high nach einem low
            while (l1 > h1);

    }
// Überprüfen ob entladen werden kann
    x1 = Highprice(0,l1,w[0].pp);  // wieviel Einträge sind höher mit dem SoC in Consumption abgleichen
//    printf("%0.02f %0.02f %0.02f %0.02f \n",(fSoC-x1*fConsumption),w[0].pp,w[l1].pp*aufschlag+Diff,low2.pp*aufschlag+Diff);
    if (float(fSoC-x1*fConsumption) > 0) // x1 Anzahl der Einträge mit höheren Preisen
//        if ((w[0].pp>w[l1].pp*aufschlag+Diff)||(w[0].pp>low2.pp*aufschlag+Diff))
            if ((w[0].pp>w[l1].pp*aufschlag+Diff)) // Nur das folgende Tief zum Entladen berücksichtigen
        return 1;
    return 0;  // kein Ergebniss gefunden

}


            
            
            
void aWATTar(std::vector<watt_s> &ch)
/*
 
 Diese Routine soll beim Programmstart und bei Änderungen in der
 Datei e3dc.wallbox.txt
 und dann 1x Stunde ausgeführt werden.
 
 
 */
{
//    std::vector<watt_s> ch;  //charge hour
bool simu = false;
int ladedauer = 4;
    time_t rawtime;
    struct tm * ptm;
    float pp;
    FILE * fp;
    char line[256];
    time(&rawtime);
    uint64_t von, bis;
    if (simu) {
    von = (rawtime-30*24*3600)*1000;
    bis = rawtime*1000;
    } else {
        von = (rawtime-rawtime%3600)*1000;
        bis = rawtime-rawtime%24*3600;
        bis = (bis + 48*3600)*1000;
    }
    ptm = gmtime (&rawtime);
    if (((ptm->tm_hour!=oldhour))||((ptm->tm_hour>=12)&&(ptm->tm_min%10==0)&&(w.size()<=12)))
    {
    oldhour = ptm->tm_hour;

//    system("curl -X GET 'https://api.awattar.de/v1/marketdata'| jq .data| jq '.[]' | jq '.start_timestamp%86400000/3600000, .marketprice'> awattar.out");
// es wird der orginale Zeitstempel übernommen um den Ablauf des Zeitstempels zu erkennen
//    system("curl -X GET 'https://api.awattar.de/v1/marketdata'| jq .data| jq '.[]' | jq '.start_timestamp/1000, .marketprice'> awattar.out");
    sprintf(line,"curl -X GET 'https://api.awattar.de/v1/marketdata?start=%llu&end=%llu'| jq .data| jq '.[]' | jq '.start_timestamp/1000, .marketprice'> awattar.out",von,bis);
        fp = fopen("debug.out","w");
        fprintf(fp,"%s",line);
        fclose(fp);
if (not simu)
        system(line);
//    system ("pwd");
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
    } else return;
    if (w.size() == 0)
    return;
    
    if (simu)
    { // simulation ausführen
        float fSoC = 57;
        float fmaxSoC = 70;
        float fConsumption = 7;
        float Diff = 32;
        float aufschlag = 1.2;
        float ladeleistung = 3000/13.8/10;
        float geladen = 0;
        float entladen = 0;
        float direkt = 0;
        float vergleich = 0;
        float wertgeladen = 0;
        float wertentladen = 0;
        float wertdirekt = 0;
        float wertvergleich = 0;
        int ret;
        while (w.size()>0)
    {
        ret = CheckaWATTar(0,0,fSoC,fmaxSoC,fConsumption,Diff,aufschlag, ladeleistung);
        if (ret == 0)       {
            direkt = direkt + fConsumption;
            wertdirekt= wertdirekt + fConsumption * w[0].pp/1000;
        }
        if (ret == 1) {
            if (fSoC>0)
            {
            entladen = entladen + fConsumption;
            wertentladen = wertentladen + fConsumption * w[0].pp/1000;
            fSoC = fSoC - fConsumption;
            }
        }
        if (ret == 2) {
            if (fSoC<fmaxSoC) {
            geladen = geladen + ladeleistung;
            wertgeladen = wertgeladen + ladeleistung * (w[0].pp*aufschlag+Diff)/1000;
            fSoC = fSoC + ladeleistung;}
            direkt = direkt + fConsumption;
            wertdirekt= wertdirekt + fConsumption * w[0].pp/1000;
            ;}

        vergleich = vergleich + fConsumption;
        wertvergleich= wertvergleich + fConsumption * w[0].pp/1000;

        printf("%0.3f geladen %0.3f entladen %0.3f direkt %0.3f average %0.3f vergleich %0.3f %0.4f\n",fSoC,wertgeladen/geladen,wertentladen/entladen,wertdirekt/direkt,(wertgeladen+wertdirekt)/(geladen+direkt),wertvergleich/vergleich,w[0].pp/1000);
        w.erase(w.begin());
    }// fConsumption }
    }

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

    fp = fopen("e3dc.wallbox.txt","r");
    if (fp)
    {
        (fgets(line, sizeof(line), fp)); // Nur eine Zeile mit dem Angabe der Ladedauer lesen
        ladedauer = atoi(line);
        fclose(fp);
    };

   
    von = rawtime;
    if (von%24*3600/3600<19) von = von - von%24*3600+20*3600;
    bis = von - von%24*3600 + 44*3600;

    von = w[0].hh;
    bis = w[w.size()-1].hh;

    long k;       // bis zu     if (k > 7) k = 24-k+7;
    // ersten wert hinzufügen
    
 
        pp = -1000;
        ch.clear();
 
    for (int l = 0;(l < ladedauer)&&(l< w.size()); l++)
    {
        ww.pp = 1000;

        for (int j = 0; j < w.size(); j++ )
        {
            
            if ((w[j].pp>pp)&&(w[j].pp<ww.pp)&&(w[j].hh>=von)&&(w[j].hh<=bis))
            {
                ww =  w[j];
            }
        }
        ch.push_back(ww);
        pp = ww.pp;
        long erg = 24*3600;
    }
    fp = fopen("e3dc.wallbox.txt","w");
    fprintf(fp,"%i\n",ladedauer);
//    sort (ch.begin(),ch.end());
    std::sort(ch.begin(), ch.end(), [](const watt_s& a, const watt_s& b) {
        return a.hh < b.hh;});
    for (int j = 0; j < ch.size(); j++ ){
//        k = (ch[j].hh% (24*3600)/3600);
        ptm = localtime(&ch[j].hh);
//        fprintf(fp,"%i %.2f; ",k,ch[j].pp);
        fprintf(fp,"%i.%i. %i:00 %.2f; ",ptm->tm_mday,ptm->tm_mon+1,ptm->tm_hour,ch[j].pp);
    }
    fprintf(fp,"%s\n",ptm->tm_zone);
    fclose(fp);
    CheckWallbox();
    }
