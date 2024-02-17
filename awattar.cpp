//
//  aWATTar.cpp
//  
//
//  Created by Eberhard Mayer on 26.11.21.
//  Version 16.2.2022
//  Copyright © 2021 Eberhard Mayer. All rights reserved.
//  

#include "awattar.hpp"
#include "E3DC_CONF.h"
#include <sys/stat.h>
#include <cstring>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <array>
#include <cctype>
#include <time.h>
#include <array>
#include <algorithm>
#include <vector>
#include "cJSON.h"
#include <fcntl.h>

//typedef struct {int hh; float pp;}watt_s;

static time_t tm_Wallbox_dt = 0;
static watt_s ww,ww1,ww2;
static ch_s cc;
static time_t oldhour = 0; // zeitstempel Wallbox Steurungsdatei;
static int old_w_size = 0;
int Diff = 100;           // Differenz zwischen niedrigsten und höchsten Börsenwert zum der Speicher nachgeladen werden soll.
int hwert = 5; // Es wird angenommen, das pro Stunde dieser Wert aus dem Speicher entnommen wird.

static watt_s high;
static watt_s high2;
static watt_s low;
static int l1 = 0, l2 = 0, h1 = 0, h2 = 0;

bool Checkfile(char myfile[20],int minuten)
{
    struct stat stats;
     time_t  tm,tm_dt;
     time(&tm);
     if (stat(myfile,&stats)==0)
     {
         tm_dt = *(&stats.st_mtime);
         tm = (tm - tm_dt);
         if (tm >0 && tm < minuten*60)
             return false;  // zu jung
         else
             return true;
     } else return true;    // nicht vorhanden
};

bool CheckWallbox()
/*
Mit dieser Funktion wird überprüft, ob die Wallbox für das Laden zu
aWATTar -Tarifen freigeschaltet werden muss.
Wenn über den Webserver eine neue Ladedaueränderung erkannt wurde
oder jede Stunde wird aWATTar aufgerufen, um die neuen aWATTar preise zu verarbeiten.
 */
{
    struct stat stats;
     time_t  tm,tm_dt;
     time(&tm);
     stat("e3dc.wallbox.txt",&stats);
     tm_dt = *(&stats.st_mtime);
     tm = (tm - tm_dt)/10;
    if (tm > 1) tm_Wallbox_dt = tm_dt;
    if (tm_dt==tm_Wallbox_dt) // neu erstellt oder alt? nur bei änderung
    {
        return false;
    }
    else
    {
        tm_Wallbox_dt = tm_dt;
        return true;
    }
}

int Highprice(std::vector<watt_s> &w,int ab,int bis,float preis)    // Anzahle Einträge mit > preis
{                                            // l1 = erste position h1 = letzte Position
    int x1 = 0;
    for (int j = ab; (j <= bis)&&(j<w.size()); j++ )
    {
// Suchen nach Hoch und Tiefs
        if (w[j].pp > preis) x1++;
    }
    return x1;
}
float fHighprice(std::vector<watt_s> &w,int ab,int bis,float preis)    // Anzahle Einträge mit > preis
{                                            // l1 = erste position h1 = letzte Position
    float x1 = 0;
    float x2 = 0;
    float x3 = 0;
    for (int j = ab; (j <= bis)&&(j<w.size()); j++ )
    {
        x3 = w[j].hourly + w[j].wpbedarf - w[j].solar;;
        // Suchen nach Hoch und Tiefs
        if (w[j].pp > preis) {
            if (x3 > 0)
            {
                if (x2 > 0)
                    if (x2>x3)    // Konnte schon nachgeladen werden, dann vom bedarf abziehen.
                        x2 = x2 - x3;
                        else
                        {
                            x3 = x3 - x2; // solaren Zugewinn abziehen
                            x1 = x1 + x3; // Bedarf hinzurechen
                            x2 = 0;       // Zugewinn ist nun 0
                        }
                
                        // mit Solaretrag verrechnen
                else
                    x1 = x1 + x3;
            }
            else 
            {
                x2 = x2 - x3;
// Highpricesuche abbrechen
                if (x2 > 100)
                return x1;
                    
            }
        } else
            if (x3 < 0)
            {              // PV Überschuss ?
                x2 = x2 - x3;
                if (x2 > 100)
                {x2 = 100;
                    // Wenn der Speicher voll ist, kann abgebrochen werden?
                return x1;}
            }
    }
    return x1;
}
int Lowprice(std::vector<watt_s> &w,int ab,int bis,float preis)    // Anzahle Einträge mit < Preisf
{                                            // l1 = erste position h1 = letzte Position
    int x1 = 0;
    for (int j = ab; j <= bis; j++ )
    {
// Suchen nach Hoch und Tiefs
        if (w[j].pp < preis) x1++;
    }
    return x1;
}

void SucheHT(std::vector<watt_s> &w,int ab,long bis)  // ab = Index bis zeitangabe in Minuten oder Sekunden seit 1970
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
bool SucheDiff(std::vector<watt_s> &w,int ab, float aufschlag,float Diff)  // ab = Index bis Diff zwischen high und low erreicht
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


int SuchePos(std::vector<watt_s> &w,int bis)  // ab = Index bis zeitangabe in Minuten Suchen nach dem Zeitpunkt oder Sekunden nach 1970
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
    int zeit, ret=-1;
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
int SimuWATTar(std::vector<watt_s> &w,  int h, float &fSoC,float anforderung,float Diff,float aufschlag, float ladeleistung) // fConsumption Verbrauch in % SoC Differenz Laden/Endladen

// Returncode 0 = keine Aktion, 1 Batterieentladen stoppen 2 Batterie mit Netzstrom laden
{
    // Ermitteln Stundenanzahl
    // Analyseergebnisse in die Datei schreiben
    static float lowpp;
    time_t  rawtime;
    time(&rawtime);
    struct tm * ptm;
    ptm = gmtime (&rawtime);
    
    int x1,x2,x3,x4;
    int Minuten = rawtime%(24*3600)/60;
    float fConsumption;
    
    //    return 2;  // Zu testzwecken  dann 9 Minuten Netzladebetrieb
    /*
     if (Minuten%60<1)
     return 1;  else // Zu testzwecken erst eine Minute Entladen erlauben
     {
     if (Minuten%60<6)
     return 2;  // Zu testzwecken  dann 9 Minuten Netzladebetrieb
     }
     */
    
    {
        float offset;
        float SollSoc = 0;
        
        // Überprüfen ob entladen werden kann
        fConsumption = fHighprice(w,h,w.size()-1,w[h].pp);  // wieviel Einträge sind höher mit dem SoC in Consumption abgleichen
        float faval = fSoC-fConsumption;
        if (faval >=0||anforderung>=0) // x1 Anzahl der Einträge mit höheren Preisen
        {
            if (faval >= anforderung*-1||anforderung>=0)
            {
                fSoC = fSoC + anforderung;
                return 1;
            } else {
                fSoC = fSoC - faval;
                anforderung = anforderung + faval;
                
            }
        }
        
        // suche über den gesamten Bereich
        x1 = SucheDiff(w,h, aufschlag,Diff); // es wird gandenlos bis zum nächsten low entladen
        do
        {
            fConsumption = fHighprice(w,h,l1,w[h].pp);  // nächster Nachladepunkt überprüfen
            if (float(fSoC-fConsumption) > 0) // x1 Anzahl der Einträge mit höheren Preisen
                if (w[h].pp>w[l1].pp*aufschlag+Diff)
                {
                    fSoC = fConsumption;
                    return 1;
                }
            if (h1>l1)
            {if (not (SucheDiff(w,h1, aufschlag,Diff))) break;} // suche low nach einem high
            else
            {if (not (SucheDiff(w,l1, aufschlag,Diff))) break;} // suche low nach einem high
        }
        while (l1 < w.size());
        
        if (SucheDiff(w,h, aufschlag,Diff))
        {
            if (h1>l1)       // erst kommt ein low dann ein high, überprüfen ob zum low geladen werden soll
            {
                int lw = l1;
                do
                    if (not (SucheDiff(w,h1, aufschlag,Diff))) break; // suche low nach einem high
                while (h1 > l1);
                // suche das nächste low
                // suchen nach dem low before next high das low muss niedriger als das akutelle sein
                int hi = h1;
                while ((l1 > h1)||(w[0].pp<w[l1].pp)) {
                    if (h1>l1)
                    {if (not (SucheDiff(w,h1, aufschlag,Diff))) {
                        l1 = w.size()-1;
                        break;}} // suche low nach einem high
                    else
                    {if (not (SucheDiff(w,l1, aufschlag,Diff))) {
                        l1 = w.size()-1;
                        break;}} // suche low nach einem high
                }
                // Überprüfen ob Entladen werden kann
                x3 = w.size()-1;
                x1 = Lowprice(w,h, hi, w[h].pp);   // bis zum high suchen
                x2 = Lowprice(w,h, w.size()-1, w[h].pp);   // bis zum high suchen

                SollSoc = fHighprice(w,h,l1,w[h].pp*aufschlag+Diff);  // Preisspitzen, es muss mindestens eine vorliegen
                // Nachladen aus dem Netz erforderlich, wenn für die Abdeckung der Preisspitzen
                // Stunden mit hohen Börsenpreisen, Nachladen wenn SoC zu niedrig
                float SollSoc2 = fHighprice(w,h,w.size()-1,w[h].pp*aufschlag+Diff);
//                SollSoc = SollSoc +fSoC;
                if (x2 == x1) // keine weiteren Lows
//                    SollSoc = SollSoc2 +fSoC;
                SollSoc = SollSoc2;
                if (SollSoc > 95) SollSoc = 95;
                if ((SollSoc>fSoC+0.5)&&        // Damit es kein Überschwingen gibt, wird 1% weniger als das Soll geladen
                    ((x1==0)||((SollSoc-fSoC)>x1*ladeleistung)))      // Stunden mit hohen Börsenpreisen, Nachladen wenn SoC zu niedrig
                {

                    if ((SollSoc-fSoC)>ladeleistung)
                        fSoC = fSoC + ladeleistung;
                    else
                        fSoC = SollSoc;
//                    if (fSoC > SollSoc-0.5)
//                        (fSoC = SollSoc-0.5);
                    return 2;}
                else
                    //                if ((SollSoc)>fSoC-1)
                    return 0; // Nicht entladen da die Preisdifferenz zur Spitze noch zu groß
            }
        }
        //        if (taglaenge > Wintertag) // tagsüber noch hochpreise es werden mind. die 2h nach sonnaufgang geprüft
        {
            fConsumption = fHighprice(w,h,w.size()-1,w[h].pp);  // folgender Preis höher, dann anteilig berücksichtigen
            
            
            if (float(fSoC-fConsumption) >=0) // x1 Anzahl der Einträge mit höheren Preisen
                return 1;
        }
        
        return 0;  // kein Ergebniss gefunden
        
        
        return 0;
        
    }}

int CheckaWATTar(std::vector<watt_s> &w,int sunrise,int sunset,int sunriseWSW, float fSoC,float fmaxSoC,float fConsumption,float Diff,float aufschlag, float ladeleistung,int mode,float &fstrompreis, float Reserve) // fConsumption Verbrauch in % SoC Differenz Laden/Endladen

// Returncode 0 = keine Aktion, 1 Batterieentladen stoppen 2 Batterie mit Netzstrom laden
{
// Ermitteln Stundenanzahl
// Analyseergebnisse in die Datei schreiben
    static float lowpp;
    time_t  rawtime;
    time(&rawtime);
    struct tm * ptm;
    ptm = gmtime (&rawtime);

    int x1,x2,x3,x4;
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
    ladeleistung = ladeleistung*.9; // Anpassung Wirkungsgrad
    int taglaenge = sunset-sunrise;
    int tagoffset = 12*60-taglaenge;
    tagoffset = tagoffset/2;
    if (tagoffset < 0) tagoffset = 0;

if (mode == 0) // Standardmodus
{
    float offset;
    float SollSoc = 0;

// Überprüfen ob entladen werden kann
        fConsumption = fHighprice(w,0,w.size()-1,w[0].pp);  // wieviel Einträge sind höher mit dem SoC in Consumption abgleichen
        if (float(fSoC-fConsumption) >=Reserve) // x1 Anzahl der Einträge mit höheren Preisen
            return 1;

// suche über den gesamten Bereich
        x1 = SucheDiff(w,0, aufschlag,Diff); // es wird gandenlos bis zum nächsten low entladen
        do
        {
            fConsumption = fHighprice(w,0,l1,w[0].pp);  // nächster Nachladepunkt überprüfen
            if (float(fSoC-fConsumption) > 1) // x1 Anzahl der Einträge mit höheren Preisen
            if ((w[0].pp>w[l1].pp*aufschlag+Diff)&&fSoC>=Reserve)
                return 1;
            if (h1>l1)
                {if (not (SucheDiff(w,h1, aufschlag,Diff))) break;} // suche low nach einem high
            else
                {if (not (SucheDiff(w,l1, aufschlag,Diff))) break;} // suche low nach einem high
        }
        while (l1 < w.size());
    
    if (SucheDiff(w,0, aufschlag,Diff))
    {
        if (h1>l1)       // erst kommt ein low dann ein high, überprüfen ob zum low geladen werden soll
        {
            int lw = l1;
            do
            if (not (SucheDiff(w,h1, aufschlag,Diff))) break; // suche low nach einem high
            while (h1 > l1);
// suche das nächste low
            // suchen nach dem low before next high das low muss niedriger als das akutelle sein
            int hi = h1;
            while ((l1 > h1)||(w[0].pp<w[l1].pp)) {
                if (h1>l1)
                {if (not (SucheDiff(w,h1, aufschlag,Diff))) {
                    l1 = w.size()-1;
                    break;}} // suche low nach einem high
                else
                {if (not (SucheDiff(w,l1, aufschlag,Diff))) {
                    l1 = w.size()-1;
                    break;}} // suche low nach einem high
            }
// Überprüfen ob Entladen werden kann
            x1 = Lowprice(w,0, hi, w[0].pp);   // bis zum high suchen
            x3 = Lowprice(w,0, w.size()-1, w[0].pp);   // bis zum high suchen
            SollSoc = fHighprice(w,0,l1,w[0].pp*aufschlag+Diff);  // Preisspitzen, es muss mindestens eine vorliegen
            float SollSoc2 = fHighprice(w,0,w.size()-1,w[0].pp*aufschlag+Diff);  // Preisspitzen, es muss mindestens eine                                             // Nachladen aus dem Netz erforderlich, wenn für die Abdeckung der Preisspitzen

            if (x1==x3) {
                if (SollSoc2>SollSoc)
//                    SollSoc = SollSoc2 + fSoC;
                SollSoc = SollSoc2;

            } 
//            else
//                SollSoc = SollSoc + fSoC;

            if ((ptm->tm_hour*60+ptm->tm_min)>(sunrise)&&(ptm->tm_hour*60+ptm->tm_min)<(sunset-120)&&(SollSoc > (fmaxSoC-1)))
                SollSoc = fmaxSoC-1;  //tagsüber laden bis 2h vor sonnenuntergang auf Reserve beschränken
            if (SollSoc > 95) SollSoc = 95;
            // Der Speicher soll nicht leer herumstehen, zum Tiefstkurs laden.
            if ((SollSoc < 0.5)&&(fSoC < 0.5)&&(lw==0)) SollSoc = 1;
            if ((SollSoc>fSoC+0.5)&&        // Damit es kein Überschwingen gibt, wird 2% weniger als das Soll geladen
                ((lw==0)||((SollSoc-fSoC)>x1*ladeleistung)))      // Stunden mit hohen Börsenpreisen, Nachladen wenn SoC zu niedrig
            {
                return 2;}
            else
//                if ((SollSoc)>fSoC-1) 
                    return 0; // Nicht entladen da die Preisdifferenz zur Spitze noch zu groß
        }
    }
//        if (taglaenge > Wintertag) // tagsüber noch hochpreise es werden mind. die 2h nach sonnaufgang geprüft
    {
            fConsumption = fHighprice(w,0,w.size()-1,w[0].pp);  // folgender Preis höher, dann anteilig berücksichtigen

        
    if (float(fSoC-fConsumption) >=Reserve) // x1 Anzahl der Einträge mit höheren Preisen
    return 1;
    }

    return 0;  // kein Ergebniss gefunden


return 0;

}

if (mode == 1) // Es wird nur soviel nachgeladen, wie es ausreichend ist um die
    {
         float offset;
        float SollSoc = 0;
//                if (taglaenge > Wintertag)
            offset = (cos((ptm->tm_yday+9)*2*3.14/365));
//            offset = (cos((69)*2*3.14/365));
            if (offset > 0)
            offset = pow(abs(offset),3.5)*(24*60-sunrise);
//            if (offset < ioffset) offset = ioffset;

// Überprüfen ob entladen werden kann
            fConsumption = fHighprice(w,0,w.size()-1,w[0].pp);  // wieviel Einträge sind höher mit dem SoC in Consumption abgleichen
            if (float(fSoC-fConsumption) >=0) // x1 Anzahl der Einträge mit höheren Preisen
                return 1;

// suche über den gesamten Bereich
            x1 = SucheDiff(w,0, aufschlag,Diff); // es wird gandenlos bis zum nächsten low entladen
            do
            {
                fConsumption = fHighprice(w,0,l1,w[0].pp);  // nächster Nachladepunkt überprüfen
                if (float(fSoC-fConsumption) > 1) // x1 Anzahl der Einträge mit höheren Preisen
                if (w[0].pp>w[l1].pp*aufschlag+Diff)
                    return 1;
                if (h1>l1)
                    {if (not (SucheDiff(w,h1, aufschlag,Diff))) break;} // suche low nach einem high
                else
                    {if (not (SucheDiff(w,l1, aufschlag,Diff))) break;} // suche low nach einem high
            }
            while (l1 < w.size());
        
        if (SucheDiff(w,0, aufschlag,Diff))
        {
            if (h1>l1)       // erst kommt ein low dann ein high, überprüfen ob zum low geladen werden soll
            {
                int lw = l1;
                do
                if (not (SucheDiff(w,h1, aufschlag,Diff))) break; // suche low nach einem high
                while (h1 > l1);
// suche das nächste low
                // suchen nach dem low before next high das low muss niedriger als das akutelle sein
                int hi = h1;
                while ((l1 > h1)||(w[0].pp<w[l1].pp)) {
                    if (h1>l1)
                    {if (not (SucheDiff(w,h1, aufschlag,Diff))) {
                        l1 = w.size()-1;
                        break;}} // suche low nach einem high
                    else
                    {if (not (SucheDiff(w,l1, aufschlag,Diff))) {
                        l1 = w.size()-1;
                        break;}} // suche low nach einem high
                }
                    // Wenn das neue Low ein Preispeak ist, dann weitersuchen
//                if ((w[0].pp*aufschlag+Diff)<w[l1].pp)
//                if (w[0].pp<w[l1].pp)
//                    SucheDiff(h1, aufschlag,Diff);
    // Überprüfen ob Entladen werden kann
// Vor Sonnenaufgang? Bei Taglänge > 10h wird nur noch die Morgenspitze berücksichtigt
                x3 = w.size()-1;
// Wenn die aktuelle tagelänge kleiner ist als die Vorgabe im Wintertag
                {
                    if ((ptm->tm_hour*60+ptm->tm_min)<(sunrise+offset))
                        x3 = SuchePos(w,sunrise+offset+60); // eine Stunde weiter suhen
                    else
                        x3 = SuchePos(w,sunrise+25*60+offset);
                    if (x3 > 0)
                        x3--;
                    if (x3<l1&&x3>=0) l1 = x3;
                    // SollSoC minutengenau berechnen X3 ist die letzte volle Stunde
                if (w[0].pp*aufschlag+Diff<w[l1].pp)
                    {
                        SollSoc = ((sunrise+int(offset))%60);
                        SollSoc = SollSoc/60;
                        SollSoc = SollSoc*(w[l1].hourly+w[l1].wpbedarf);
                    }
                }

                x1 = Lowprice(w,0, hi, w[0].pp);   // bis zum high suchen
                fConsumption = fHighprice(w,0,l1,w[0].pp*aufschlag+Diff);  // Preisspitzen, es muss mindestens eine vorliegen
                                                // Nachladen aus dem Netz erforderlich, wenn für die Abdeckung der Preisspitzen
    //            if (((fSoC < (x2*fConsumption+5))&&((l1==0)||(x2*fConsumption-fSoC)>x1*23))&&(fSoC<fmaxSoC-1))      // Stunden mit hohen Börsenpreisen, Nachladen wenn SoC zu niedrig
                SollSoc = SollSoc+fConsumption;
                float SollSoc2 = fSoC;
                for (int j=0;j<=x3;j++) // Simulation
                {
                    if (w[j].pp < w[0].pp&&SollSoc2<0) break; // war schon überzogen Abruch
                    if (w[j].pp < w[0].pp) SollSoc2 = SollSoc2 + ladeleistung;
                    if (w[j].pp > w[0].pp*aufschlag+Diff) SollSoc2 = SollSoc2 - w[j].hourly-w[j].wpbedarf;
                    if (SollSoc2 > fmaxSoC-1||SollSoc2<ladeleistung*-1) break;
                }
                if (SollSoc2 < 0){
                    SollSoc2 = fSoC-SollSoc2;
                    if (SollSoc2 > SollSoc)
                        SollSoc = SollSoc2;}
                if ((ptm->tm_hour*60+ptm->tm_min)>(sunrise)&&(ptm->tm_hour*60+ptm->tm_min)<(sunset)&&(SollSoc > (fmaxSoC-1)))
                    SollSoc = fmaxSoC-1;  //tagsüber laden auf Reserve beschränken
                if ((SollSoc>fSoC+1)&&        // Damit es kein Überschwingen gibt, wird 1% weniger als das Soll geladen
                    ((lw==0)||((SollSoc-fSoC-1)>x1*ladeleistung)))      // Stunden mit hohen Börsenpreisen, Nachladen wenn SoC zu niedrig
                {
                    return 2;}
                else
                    if ((SollSoc)>fSoC-1) return 0; // Nicht entladen da die Preisdifferenz zur Spitze noch zu groß
//                if ((SollSoc+1)>fSoC) return 0; // Nicht entladen da die Preisdifferenz zur Spitze noch zu groß
            }
        }
//        if (taglaenge > Wintertag) // tagsüber noch hochpreise es werden mind. die 2h nach sonnaufgang geprüft
        {
//            float offset = (cos((ptm->tm_yday+9)*2*3.14/365));
//            offset = pow(offset,3.5)*(24*60-sunrise);
//            if (offset < ioffset) offset = ioffset;
            if ((ptm->tm_hour*60+ptm->tm_min)<(sunrise))  // bis Sonnenaufgang
                x2 = SuchePos(w,sunrise+offset+60); // Suchen bis 2h nach Sonnenaufgang
            else
            {
                if ((ptm->tm_hour*60+ptm->tm_min)<(sunrise+offset)) // Zwischen Sonnenaufgang + offset entladen generell freigegben
                    return 1; // Suchen bis 2h nach Sonnenaufgang
                else
                    if ((ptm->tm_hour*60+ptm->tm_min)<(sunrise+offset))
                        x2 = SuchePos(w,sunrise+offset+60);
                    else
                        x2 = SuchePos(w,sunrise+25*60+offset); // Nein suchen nächsten Tag bis offset + 60
            }
            if (x2<0) x2 = w.size()-1;
            if (x2 > 0)
                fConsumption = fHighprice(w,0,x2-1,w[0].pp);  // folgender Preis höher, dann anteilig berücksichtigen
            else
                x3=0;

            
            SollSoc = fHighprice(w,0,x2,w[0].pp);  // Anzahl höhere Preise ermitteln
            if (SollSoc>fConsumption)  // SollSoC minutengenau berechnen
            {
                SollSoc = ((sunrise+int(offset))%60);
                SollSoc = SollSoc/60;
                SollSoc = SollSoc*(w[x2].hourly+w[x2].wpbedarf)+fConsumption;
            }
        if (float(fSoC-SollSoc) >=0) // x1 Anzahl der Einträge mit höheren Preisen
        return 1;
        }

        return 0;  // kein Ergebniss gefunden

    }
    return 0;
}
int fp_status = -1;  //

void openmeteo(std::vector<watt_s> &w, e3dc_config_t e3dc,int anlage)
{
    FILE * fp;
    char line[256];
    char path [65000];
    char value[25];
    char var[25];
    char var2[25];

    int x1 = 0;
    int x2,x3;
    int len = strlen(e3dc.Forecast[anlage]);
    
    if (anlage >=0)
    {
        memcpy(&line,&e3dc.Forecast[anlage],len);
        memset(var, 0, sizeof(var));
        memset(var2, 0, sizeof(var2));
        memset(value, 0, sizeof(value));
        for(int j = 0;j<len&&x1==0;j++)
        {
            if (line[j]=='/') x1=j;
        }
        x2=0;
        for(int j=x1+1;j<len&&x2==0;j++)
        {
            if (line[j]=='/') x2=j;
        }
        memcpy(&var,&line[0],x1);
        memcpy(&var2,&line[x1+1],x2-x1-1);
        memcpy(&value,&line[x2+1],len-x2-1);
        
        x1 = atoi(var);
        x2 = atoi(var2);
        x3 = atoi(value);
    }

   
      {

          sprintf(line,"curl -X GET 'https://api.open-meteo.com/v1/forecast?latitude=%f&longitude=%f&minutely_15=global_tilted_irradiance_instant&timeformat=unixtime&forecast_minutely_15=192&tilt=%i&azimuth=%i'",e3dc.hoehe,e3dc.laenge,x1,x2);

          fp = NULL;
          fp = popen(line, "r");
          int fd = fileno(fp);
          int flags = fcntl(fd, F_GETFL, 0);
          flags |= O_NONBLOCK;
          fcntl(fd, F_SETFL, flags);
          fp_status = 2;

    }
    {
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
            feld = "global_tilted_irradiance_instant";
            c = &feld[0];
            item2 = cJSON_GetObjectItemCaseSensitive(item, c );
            item1 = item1->child;
            item2 = item2->child;
            int x1 = 0;
            while (item1!=NULL)
            {
                while (w[x1].hh < item1->valueint&&x1<w.size())
                    x1++;
                if (w[x1].hh == item1->valueint)
                {
                    if (anlage==0)
                        w[x1].solar = item2->valuedouble*x3/4/e3dc.speichergroesse/10;  // (15min Intervall daher /4
                    else
                        w[x1].solar = w[x1].solar+item2->valuedouble*x3/4/e3dc.speichergroesse/10;
                    x1++;
                    if (x1 >= w.size())
                        return;
                }
                item1 = item1->next;
                item2 = item2->next;

            }

        }
    }
        
}
            
void forecast(std::vector<watt_s> &w, e3dc_config_t e3dc_config,int anlage)


{
    FILE * fp;
    char line[256];
    int x3,x2;
    char value[256];
    char var[256];
    sprintf(var,"forecast%i.json",anlage);
    if (Checkfile(var,59)){
        sprintf(line,"curl -X GET 'https://api.forecast.solar/estimate/%f/%f/%s?time=seconds'| jq . > %s",e3dc_config.hoehe,e3dc_config.laenge,e3dc_config.Forecast[anlage],var);
        
        int res = system(line);
    }
    sprintf(line,"jq .result %s| jq .watt_hours_period > forecast.json",var);

     int res = system(line);

    
    
    fp = fopen("forecast.json","r");
    if(fp)
    {
        int w1 = 0;
        while (fgets(line, sizeof(line), fp))
        {
            long x1 = 0;
            int x2,x3;
            int len = strlen(line);
            if (strlen(line) > 10)
            {
                memset(var, 0, sizeof(var));
                memset(value, 0, sizeof(value));
                for(int j = 0;j<len&&x1==0;j++)
                {
                    if (line[j]=='"') x1=j;
                }
                x2=0;
                for(int j=x1+1;j<len&&x2==0;j++)
                {
                    if (line[j]=='"') x2=j;
                }
                x3 = x2-x1-1;
                if (x3 < sizeof(line))
                memcpy(&var,&line[x1+1],x3);
                x1=0;
                for(int j=x2;j<len&&x1==0;j++)
                {
                    if (line[j]==':') x1=j;
                }
                x3=0;
                for(int j=x1;j<len&&x3==0;j++)
                {
                    if (line[j]==','||line[j]=='\n') x3=j;
                }
                memcpy(&value,&line[x1+1],x3-x1-1);
                
                x1 = atol(var);
                x2 = atoi(value);
                {
                    time_t time = w[w1].hh;
                    while (w[w1].hh<x1&&w1<w.size())
                        w1++;
                    
                    
                    if (w[w1].hh == x1)
                    {
// den PV-Ertrag immer der vorhergehende Stunde zuordnen
                        if (w1>0)
                        {
                            int x2 = x1%(24*3600)/60;
                            int x3 = x2 - sunriseAt;
                            float pv = atof(value);
                            float pv2 = w[w1-1].solar;
                            if (x3>0&&x3<=60)
                            {
                                if (x3 <=30)
                                    pv = pv/10; // die ersten 30min 10%
                                else;
                                if (x3 <=60)
                                {
                                    pv2 = pv / x3;
                                    pv = pv2 * 3 + pv2 * (x3-30)/2;
                                }
                            }
                            if (x3>60&&x3<=120)
                            {
                                x3 = 120-x3;
                                pv2 = pv/60; // die ersten 30min 10%
                                if (x3>30)
                                {
                                    pv = (x3-30)*pv2/10;
                                    pv = pv + 30*pv2/2;
                                }
                                if (x3<=30)
                                    pv = pv = x3*pv2/2;
                                pv = pv + (60 - x3)*pv2;
                            }

                            pv = pv/e3dc_config.speichergroesse/10;

                            if (anlage == 0)
                                w[w1-1].solar = pv;
                            else
                                w[w1-1].solar = w[w1-1].solar + pv;
                        }
                            w1++;
                    }
                }
            }
        }
    };
};
            
//void aWATTar(std::vector<watt_s> &ch, int32_t Land, int MWSt, float Nebenkosten)
void aWATTar(std::vector<ch_s> &ch,std::vector<watt_s> &w, e3dc_config_t &e3dc, float soc, int sunrise)
/*
 
 Diese Routine soll beim Programmstart und bei Änderungen in der
 Datei e3dc.wallbox.txt
 und dann 1x Stunde ausgeführt werden. aktuell bei open-meteo alle 15min
 
 
 */
{
//    std::vector<watt_s> ch;  //charge hour
bool simu = false;
//    simu = true;
int ladedauer = 0;
    float strombedarf[24];
    time_t rawtime;
    struct tm * ptm;
//    float pp;
    FILE * fp;
    char line[256];
    int x3,x2;
    char value[256];
    char var[256];
    int64_t von, bis;
//    e3dc_config_t e3dc;
    
    time(&rawtime);
    ptm = gmtime (&rawtime);
    sunriseAt = sunrise;
// alte Einträge > 1h löschen
    if (e3dc.openmeteo)
        while ((not simu)&&w.size()>0&&(w[0].hh+900<=rawtime))
        w.erase(w.begin());
    else
        while ((not simu)&&w.size()>0&&(w[0].hh+3600<=rawtime))
        w.erase(w.begin());


// Tagesverbrauchsprofil aus e3dc.config.txt AWhourly vorbelegen


    
    if (((rawtime-oldhour)>=3600&&ptm->tm_min==0)
        ||
        ((ptm->tm_hour>=12)&&(ptm->tm_min%5==0)&&(ptm->tm_sec==0)&&(w.size()<12))
        || 
        (e3dc.openmeteo&&((rawtime-oldhour)>=900)&&ptm->tm_min%15==0)
        ||
        (e3dc.openmeteo&&(ptm->tm_hour>=12)&&(ptm->tm_min%5==0)&&(ptm->tm_sec==0)&&(w.size()<48))
        ||
        (w.size()==0)
        )
    {
        oldhour = rawtime;
//        old_w_size = w.size();

        for (int j1 = 0;j1<24;j1++) {
            strombedarf[j1] = e3dc.Avhourly;
        }
    // Tagesverbrauchsprofil einlesen.
        printf("\ne3ec.hourly\n");
        fp = fopen("e3dc.hourly.txt","r");
        if (fp){
            while (fgets(line, sizeof(line), fp))
            {
                memset(var, 0x00, sizeof(var));
                memset(value, 0x00, sizeof(value));
                sscanf(line, "%s %s", var, value);
                //            sscanf(line, "%[^ \t=]%*[\t ]=%*[\t ]%[^\n]", var, value);
                x2 = atoi(var);
                if (x2>=0&&x2<24&&strlen(var)>0)
                    if (atof(value) < 100)
                        strombedarf[x2] = atof(value);
            }
            fclose(fp);

            printf("e3ec.hourly done\n");
            for (int j=0;j<w.size();j++)
            {
                x2 =w[j].hh%(24*3600);
                x2 = x2/3600;
                if (e3dc.openmeteo)
                    w[j].hourly = strombedarf[x2]/4;
                else
                    w[j].hourly = strombedarf[x2];

            }

        }
        
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
        

        
//    system("curl -X GET 'https://api.awattar.de/v1/marketdata'| jq .data| jq '.[]' | jq '.start_timestamp%86400000/3600000, .marketprice'> awattar.out");
//    if (ptm->tm_hour%3 == 0) // Alle 3 Stunden
//        system("curl -X GET 'https://api.openweathermap.org/data/2.5/onecall?lat=50.2525&lon=10.3083&appid=615b8016556d12f6b2f1ed40f5ab0eee' | jq .hourly| jq '.[]' | jq '.dt%259200/3600, .clouds'>weather.out");
// es wird der orginale Zeitstempel übernommen um den Ablauf des Zeitstempels zu erkennen
//    system("curl -X GET 'https://api.awattar.de/v1/marketdata'| jq .data| jq '.[]' | jq '.start_timestamp/1000, .marketprice'> awattar.out");
printf("GET api.awattar\n");


if (e3dc.AWLand == 1)
        sprintf(line,"curl -X GET 'https://api.awattar.de/v1/marketdata?start=%llu&end=%llu'| jq .data| jq '.[]' | jq '.start_timestamp/1000, .marketprice'> awattar.out",von,bis);
if (e3dc.AWLand == 2)
        sprintf(line,"curl -X GET 'https://api.awattar.at/v1/marketdata?start=%llu&end=%llu'| jq .data| jq '.[]' | jq '.start_timestamp/1000, .marketprice'> awattar.out",von,bis);
        if ((not simu)&&
            (
            (w.size()<12)
            ||
            (e3dc.openmeteo&&w.size()<48)
            )
            ) // alte aWATTar Datei verarbeiten
            {
                fp = fopen("debug.out","w");
                fprintf(fp,"%s",line);
                fclose(fp);
                int res = system(line);
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
                                    x2 =ww.hh%(24*3600);
                                    x2 = x2/3600;
                                    if (e3dc.openmeteo)
                                        ww.hourly = strombedarf[x2]/4;
                                    else
                                        ww.hourly = strombedarf[x2];
                                    ww.solar = 0;
                                    if ((simu)||(ww.hh+3600>rawtime))
                                    {
                                        if (e3dc.openmeteo)
                                        {
                                            for (int x1=0;x1<=3;x1++)
                                            {
                                                w.push_back(ww);
                                                ww.hh = ww.hh + 900;
                                            }
                                        }
                                        else
                                            w.push_back(ww);

                                    }
                                } else break;
                            }

                            fclose(fp);

                        };
                        }

            }

if (e3dc.openmeteo)
{
//    openmeteo(w, e3dc, -1);  // allgemeine Wetterdaten einlesen wie Temperatur
    for (int j=0;(strlen(e3dc.Forecast[j])>0)&&j<4;j++)
    openmeteo(w, e3dc, j);
}
else
        
        for (int j=0;(strlen(e3dc.Forecast[j])>0)&&j<4;j++)
            forecast(w, e3dc, j);

    }


    
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
            ret = CheckaWATTar(w,0,0,0, fSoC,fmaxSoC,fCharge,Diff,aufschlag, ladeleistung,1,strompreis,e3dc.AWReserve);
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

        printf("%li %0.3f geladen %0.3f entladen %0.3f direkt %0.3f average %0.3f vergleich %0.3f %0.4f\n",w[0].hh%(24*3600)/3600 ,fSoC,wertgeladen/geladen,wertentladen/entladen,wertdirekt/direkt,(wertgeladen+wertdirekt)/(geladen+direkt),wertvergleich/vergleich,w[0].pp/1000);
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
//
    
    
/* die gewünschten Ladezeiten werden ermittelt
 Die neuen Ladezeiten werden ermittelt, wenn entweder
 Checkwallbox erfolgreich ist oder sich bei der Dauereinstelleung
 wbhour, wbvon oder wbbis sich geändert haben
*/
    int chch; // 0 normal 1 Automatik
    static int dauer = -1;
    if ((CheckWallbox()))
    {
        fp = fopen("e3dc.wallbox.txt","r");
        if (fp)
        {
            char * res = (fgets(line, sizeof(line), fp)); // Nur eine Zeile mit dem Angabe der Ladedauer lesen
            ladedauer = atoi(line);
            fclose(fp);
        };
        von = rawtime;
        if (von%24*3600/3600<19) von = von - von%24*3600+20*3600;
        bis = von - von%24*3600 + 44*3600;
        chch = 0;
        von = w[0].hh;
        bis = w[w.size()-1].hh;
    } else
    {
        // ist die ladezeit schon belegt oder abgelaufen
        if (w.size()<=old_w_size&&dauer == e3dc.wbhour+e3dc.wbvon*24+e3dc.wbbis*24*24){
            old_w_size = w.size();
            if (ch.size()>0&&ch[ch.size()-1].hh>rawtime&&ch[ch.size()-1].ch==0) // aktiver ladeauftrag
                return;
            if (ch.size()==0&&e3dc.wbhour<=0)  // nothing todo
                return;
        }
        if (w.size()>old_w_size||(dauer != e3dc.wbhour+e3dc.wbvon*24+e3dc.wbbis*24*24))
        {  // Es wurden die neuen Preise ausgelesen = neue ladezeiten ermitteln
            if (e3dc.wbhour < 0) return;  // nichts zu ermitteln;
            if (e3dc.wbvon < 0) e3dc.wbvon = 0;
            if (e3dc.wbbis > 24) e3dc.wbbis = 24;
            dauer =  e3dc.wbhour+e3dc.wbvon*24+e3dc.wbbis*24*24;
            old_w_size = w.size();
            bis = w[w.size()-1].hh;
            bis = bis - bis%(24*3600) + e3dc.wbbis*3600;
            von = w[0].hh;
            von = von - von%(24*3600) + e3dc.wbvon*3600;

            if (von > bis)
                bis = bis + 24*3600;   // nächster tag
            
            if (e3dc.wbhour > 0)
            {
                ladedauer = e3dc.wbhour;
                chch = 1;
            } else ladedauer = 0;
        } else return;
    }

    long k;       // bis zu     if (k > 7) k = 24-k+7;
    // ersten wert hinzufügen
    
 
        ww1.pp = -1000;
        ch.clear();
 
/*    for (int l = 0;(l < ladedauer)&&(l< w.size()); l++)
    {
        ww.pp = 1000;

        for (int j = 0; j < w.size(); j++ )
        {
            ww2=w[j];
            if ((w[j].pp>ww1.pp||(w[j].pp==ww1.pp&&w[j].hh>ww1.hh))
                &&(w[j].pp<ww.pp||(w[j].pp==ww.pp&&w[j].hh<ww.hh))&&(w[j].hh>=von)&&(w[j].hh<=bis))
            {
                ww =  w[j];
            }
        }
        cc.hh = ww.hh;
        cc.ch = chch;
        cc.pp = ww.pp;
        ch.push_back(cc);
        ww1=ww;
        long erg = 24*3600;
    }
 alte logik
 */
    for (int l = 0;(l< w.size()); l++)
    {
        if (w[l].hh>=von&&w[l].hh<=bis&&w[l].hh%60==0)
        {
            cc.hh = w[l].hh;
            cc.ch = chch;
            cc.pp = w[l].pp;
            ch.push_back(cc);
        }
    }
    std::sort(ch.begin(), ch.end(), [](const ch_s& a, const ch_s& b) {
        return a.pp < b.pp;});
    while (ch.size()>ladedauer)
    {
        ch.erase(ch.end()-1);
    }
        
    fp = fopen("e3dc.wallbox.txt","w");
    fprintf(fp,"%i\n",ladedauer);
//    sort (ch.begin(),ch.end());
    std::sort(ch.begin(), ch.end(), [](const ch_s& a, const ch_s& b) {
        return a.hh < b.hh;});
    int ptm_alt;
    for (int j = 0; j < ch.size(); j++ ){
//        k = (ch[j].hh% (24*3600)/3600);
        ptm = localtime(&ch[j].hh);
//        fprintf(fp,"%i %.2f; ",k,ch[j].pp);
        if ((j==0)||(j>0&&ptm->tm_mday!=ptm_alt))
// Datum und Reihenfolge ausgeben
        {
            if (j%2==1) fprintf(fp,"\n");
            fprintf(fp,"am %i.%i.\n",ptm->tm_mday,ptm->tm_mon+1);
        }
        fprintf(fp,"%i. um %i:00 zu %.3fct/kWh  ",j+1,ptm->tm_hour,ch[j].pp*(100+e3dc.AWMWSt)/1000+e3dc.AWNebenkosten);
        if (ch.size() < 10||j%2==1)
            fprintf(fp,"\n");
        ptm_alt = ptm->tm_mday;
    }
    fprintf(fp,"%s\n",ptm->tm_zone);
    fclose(fp);
    CheckWallbox();
    }
