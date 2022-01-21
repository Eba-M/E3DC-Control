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
static std::vector<watt_s> weather; // wetterdaten
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
    for (int j = ab; (j <= bis)&&(j<w.size()); j++ )
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

int CheckaWATTar(int sunrise,int sunset,float fSoC,float fmaxSoC,float fConsumption,float Diff,float aufschlag, float ladeleistung,int mode,float &fstrompreis) // fConsumption Verbrauch in % SoC Differenz Laden/Endladen

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
    fstrompreis = w[0].pp;
    int taglaenge = sunset-sunrise;
    int tagoffset = 12*60-taglaenge;
    tagoffset = tagoffset/2;
    if (tagoffset < 0) tagoffset = 0;

if (mode == 0) // Standardmodus
{
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
                                            // Nachladen aus dem Netz erforderlich, wenn für die Abdeckung der Preisspitzen
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
    if (mode == 1) // Es wird nur soviel nachgeladen, wie es ausreichend ist um die
    {
    // testroutine neue auswertung
        if (low2.pp == 0)
        {
            low2.pp = (Diff+Diff*aufschlag);
            if (SucheDiff(0, aufschlag,Diff))
                low2 = w[l1];
            if (low2.pp > (Diff+Diff*aufschlag))
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
// suche das nächste low
                // suchen nach dem low before next high das low muss niedriger als das akutelle sein
                int hi = h1;
                while ((l1 > h1)||(w[0].pp<w[l1].pp)) {
                    if (h1>l1)
                        {if (not (SucheDiff(h1, aufschlag,Diff))) break;} // suche low nach einem high
                    else
                        {if (not (SucheDiff(l1, aufschlag,Diff))) break;} // suche low nach einem high
                }
                    // Wenn das neue Low ein Preispeak ist, dann weitersuchen
//                if ((w[0].pp*aufschlag+Diff)<w[l1].pp)
//                if (w[0].pp<w[l1].pp)
//                    SucheDiff(h1, aufschlag,Diff);
    // Überprüfen ob Entladen werden kann
                x1 = Lowprice(0, hi, w[0].pp);   // bis zum high suchen
                x2 = Highprice(0,l1,w[0].pp*aufschlag+Diff);  // Preisspitzen, es muss mindestens eine vorliegen
                                                // Nachladen aus dem Netz erforderlich, wenn für die Abdeckung der Preisspitzen
    //            if (((fSoC < (x2*fConsumption+5))&&((l1==0)||(x2*fConsumption-fSoC)>x1*23))&&(fSoC<fmaxSoC-1))      // Stunden mit hohen Börsenpreisen, Nachladen wenn SoC zu niedrig
                float SollSoc = x2*fConsumption;
                if (SollSoc > fmaxSoC-1) SollSoc = fmaxSoC-1;
                if ((SollSoc>fSoC)&&        // es gibt mind. einen Wert mit dem nötigen aufschlag+Diff
                    ((lw==0)||((SollSoc-fSoC)>x1*ladeleistung*.9)))      // Stunden mit hohen Börsenpreisen, Nachladen wenn SoC zu niedrig
                {   low2 = w[0];
                    return 2;}
                else
                    if (SollSoc>fSoC) return 0; // Nicht entladen da die Preisdifferenz zur Spitze zu groß
            } else
                do
                    if (h1>l1)
                        {if (not (SucheDiff(h1, aufschlag,Diff))) break;} // suche low nach einem high
                    else
                        {if (not (SucheDiff(l1, aufschlag,Diff))) break;} // suche low nach einem high
                while (l1 > h1);
//            while ((l1 > h1)||(low2.pp<w[l1].pp));

        } else l1 = w.size()-1;
    // Überprüfen ob entladen werden kann
        x1 = Highprice(0,w.size()-1,w[0].pp);  // wieviel Einträge sind höher mit dem SoC in Consumption abgleichen
        if (float(fSoC-x1*fConsumption) > 0) // x1 Anzahl der Einträge mit höheren Preisen
//            if ((w[0].pp>w[l1].pp*aufschlag+Diff)||(w[0].pp>low2.pp*aufschlag+Diff))
//                if ((w[0].pp>w[l1].pp*aufschlag+Diff)) // Nur das folgende Tief zum Entladen berücksichtigen
            return 1;
        x1 = Highprice(0,l1,w[0].pp);  // nächster Nachladepunkt überprüfen
        if (float(fSoC-x1*fConsumption) > 0) // x1 Anzahl der Einträge mit höheren Preisen
            if (w[0].pp>w[l1].pp*aufschlag+Diff)
            return 1;
        if (SucheDiff(0, aufschlag,Diff)) // Wenn das nächste Low ein Nachladepunkt ist, überprüfen ob entladen werden kann
        {
            while (l1>h1)
             if (not (SucheDiff(l1, aufschlag,Diff))) break;
            x1 = Highprice(0,l1,w[0].pp);  // nächster Nachladepunkt überprüfen
        if (float(fSoC-x1*fConsumption) > 0) // x1 Anzahl der Einträge mit höheren Preisen
            if (w[0].pp>w[l1].pp*aufschlag+Diff)
                return 1;
            
        }
        return 0;  // kein Ergebniss gefunden

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
bool simu = false;
//    simu = true;
int ladedauer = 4;
    time_t rawtime;
    struct tm * ptm;
    float pp;
    FILE * fp;
    char line[256];
    time(&rawtime);
    ptm = gmtime (&rawtime);

    int64_t von, bis;
    if (simu)
    {
        von = (rawtime-30*24*3600)*1000;
        bis = rawtime*1000;
    } else
    {
        von = rawtime-rawtime%3600;
        von = von*1000;
        bis = rawtime-rawtime%(24*3600);
        bis = (bis + 48*3600);
        bis = bis*1000;
    }
    
    if (((ptm->tm_hour!=oldhour))||((ptm->tm_hour>=12)&&(ptm->tm_min%5==0)&&(ptm->tm_sec==0)&&(w.size()<12)))
    {
        oldhour = ptm->tm_hour;

        
        
//    system("curl -X GET 'https://api.awattar.de/v1/marketdata'| jq .data| jq '.[]' | jq '.start_timestamp%86400000/3600000, .marketprice'> awattar.out");
//    if (ptm->tm_hour%3 == 0) // Alle 3 Stunden
//        system("curl -X GET 'https://api.openweathermap.org/data/2.5/onecall?lat=50.2525&lon=10.3083&appid=615b8016556d12f6b2f1ed40f5ab0eee' | jq .hourly| jq '.[]' | jq '.dt%259200/3600, .clouds'>weather.out");
// es wird der orginale Zeitstempel übernommen um den Ablauf des Zeitstempels zu erkennen
//    system("curl -X GET 'https://api.awattar.de/v1/marketdata'| jq .data| jq '.[]' | jq '.start_timestamp/1000, .marketprice'> awattar.out");
    sprintf(line,"curl -X GET 'https://api.awattar.de/v1/marketdata?start=%llu&end=%llu'| jq .data| jq '.[]' | jq '.start_timestamp/1000, .marketprice'> awattar.out",von,bis);
        if (w.size() > 12)
        {
            if (w[0].hh+3600<rawtime)
                w.erase(w.begin());
            
        }
        else
            if ((not simu)&&(w.size()<12)) // alte aWATTar Datei verarbeiten
            {
//                fp = fopen("debug.out","w");
//                fprintf(fp,"%s",line);
//                fclose(fp);
                system(line);
                // Einlesen der letzten aWATTar Datei
                        if (not simu)
                        {
                            fp = fopen("awattar.out","r");
//                        else
//            fp = fopen("awattar.out.txt","r");
//                            fp = fopen("awattar.out","r");
                        if(fp)
                        {
                            w.clear();

                            while (fgets(line, sizeof(line), fp))
                            {

                                ww.hh = atol(line);
                                if (fgets(line, sizeof(line), fp))
                                {
                                    ww.pp = atof(line);

                                    if ((simu)||(ww.hh+3600>rawtime))
                                        w.push_back(ww);
                                } else break;
                            }

                            fclose(fp);

                        };
                    }

            }
//    system ("pwd");

    }

    
    
    while ((not simu)&&(w[0].hh+3600<rawtime)&&w.size()>0)
        w.erase(w.begin());

    
    if (simu)
    { // simulation ausführen
        float fSoC = 66;
        float fmaxSoC = 77;
        float fCharge = 5; // Speicher laden
        float fConsumption = 5;  // Verbrauch
        float Diff = 32;
        float aufschlag = 1.2;
//        float ladeleistung = 10000/35/10;
        float ladeleistung = 3000/13.8/10;
        float geladen = 0;
        float entladen = 0;
        float direkt = 0;
        float vergleich = 0;
        float wertgeladen = 0;
        float wertentladen = 0;
        float wertdirekt = 0;
        float wertvergleich = 0;
        do
        {
            if (w.size() == 0)
                fp = fopen("awattar.out.txt","r");
            if (w.size()  < 12)
            {
                do
                {
                    if (fgets(line, sizeof(line), fp))
                    {

                        ww.hh = atol(line);
                        if (fgets(line, sizeof(line), fp))
                        {
                            ww.pp = atof(line);

                            if ((simu)||(ww.hh+3600>rawtime))
                                w.push_back(ww);
                        } else
                        {
                            fclose(fp);
                            break;
                        }
                    }
                    else
                    {
                        fclose(fp);
                        break;
                    }
                    int stunde = ww.hh%(24*3600)/3600;
                }
            while (ww.hh%(24*3600)/3600 < 23);
            }
            int ret;
            float strompreis;
            ret = CheckaWATTar(0,0,fSoC,fmaxSoC,fCharge,Diff,aufschlag, ladeleistung,1,strompreis);
            if (ret == 0)
            {
                direkt = direkt + fConsumption;
                wertdirekt= wertdirekt + fConsumption * w[0].pp/1000;
            }
            if (ret == 1)
            {
                fSoC = fSoC - (fConsumption);
                if (fSoC < 0) {
                    entladen = entladen + fConsumption +fSoC;
                    wertentladen = wertentladen + (fConsumption +fSoC)* w[0].pp/1000;
                    fSoC = 0;

                } else
                {
                    entladen = entladen + fConsumption;
                    wertentladen = wertentladen + (fConsumption) * w[0].pp/1000;

                }
        }
        if (ret == 2)
        {
            fSoC = fSoC + ladeleistung*.9;
            direkt = direkt + fConsumption;
            wertdirekt= wertdirekt + fConsumption * w[0].pp/1000;
            if (fSoC<fmaxSoC)
            {
                geladen = geladen + ladeleistung*.9;
                wertgeladen = wertgeladen + ladeleistung*.9 * (w[0].pp*aufschlag+Diff)/1000;
                direkt = direkt + ladeleistung;
                wertdirekt = wertdirekt + ladeleistung*.9 * (w[0].pp*aufschlag+Diff)/1000;
            } else
            {
                wertgeladen = wertgeladen + (fSoC-fmaxSoC) * (w[0].pp*aufschlag+Diff)/1000;
                geladen = geladen +(fSoC-fmaxSoC);
                wertdirekt = wertdirekt + (fSoC-fmaxSoC) * (w[0].pp*aufschlag+Diff)/1000;
                direkt = direkt +(fSoC-fmaxSoC);
                fSoC = fmaxSoC;
            }
        }
            vergleich = vergleich + fConsumption;
            wertvergleich= wertvergleich + fConsumption * w[0].pp/1000;

        printf("%i %0.3f geladen %0.3f entladen %0.3f direkt %0.3f average %0.3f vergleich %0.3f %0.4f\n",w[0].hh%(24*3600)/3600 ,fSoC,wertgeladen/geladen,wertentladen/entladen,wertdirekt/direkt,(wertgeladen+wertdirekt)/(geladen+direkt),wertvergleich/vergleich,w[0].pp/1000);
        w.erase(w.begin());
    }// fConsumption }
        while (w.size()>0);

    }

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
    int ptm_alt;
    for (int j = 0; j < ch.size(); j++ ){
//        k = (ch[j].hh% (24*3600)/3600);
        ptm = localtime(&ch[j].hh);
//        fprintf(fp,"%i %.2f; ",k,ch[j].pp);
        if ((j==0)||(j>0&&ptm->tm_mday!=ptm_alt))
        fprintf(fp,"am %i.%i. um %i:00 zu %.3fct/kWh; ",ptm->tm_mday,ptm->tm_mon+1,ptm->tm_hour,ch[j].pp/10);
        else
        fprintf(fp,"um %i:00 zu %.2fct/kWh; ",ptm->tm_hour,ch[j].pp/10);
        ptm_alt = ptm->tm_mday;
    }
    fprintf(fp,"%s\n",ptm->tm_zone);
    fclose(fp);
    CheckWallbox();
    }
