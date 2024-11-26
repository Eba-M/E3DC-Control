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
#include <thread>

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

bool CheckWallbox(char file[128])
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
     stat(file,&stats);
     tm_dt = *(&stats.st_mtime);
     tm = (tm - tm_dt);
    if (tm > 60) tm_Wallbox_dt = tm_dt; //älter als 10s?
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
bool PutWallbox(std::vector<ch_s> &ch)
{
    FILE *mfp;
    char path[100];
    mfp = NULL;
    mfp = fopen("e3dc.wallbox.out","w");
    if (mfp)
    if (ch.size()>0)
        for (int j = 0; j < ch.size(); j++ ){
            fprintf(mfp,"%0.2f %i %i %0.2f \n",float((ch[j].hh%(24*3600))/3600.0), ch[j].hh,ch[j].ch,ch[j].pp);}
    
    if (mfp)
    fclose(mfp);

    return true;}

bool GetWallbox(std::vector<ch_s> &ch)
{
    FILE *mfp;
    mfp = NULL;
    mfp = fopen("e3dc.wallbox.out","r");
    char path[100];
    int status;
    char var [4] [20];
    memset(path, 0x00, sizeof(path));

    if (mfp != NULL)
        while (fgets(path, sizeof(path), mfp) != NULL)
        {
            status = sscanf(path, "%s %s %s %s", var[3], var[0], var[1], var[2]);
            cc.hh = atoi(var[0]);
            cc.ch = atoi(var[1]);
            cc.pp = atof(var[2]);
            ch.push_back(cc);
        }
    //        if (WP_status < 2)
    if (mfp != NULL)
        pclose(mfp);

    return true;}
int Highprice(std::vector<watt_s> &w,int ab,int bis,float preis)    // Anzahle Einträge mit > preis
{                                            // l1 = erste position h1 = letzte Position
                                             // maxsoc = max erreichter Soc
    int x1 = 0;
    for (int j = ab; (j <= bis)&&(j<w.size()); j++ )
    {
// Suchen nach Hoch und Tiefs
        if (w[j].pp > preis) x1++;
    }
    return x1;
}
float fHighprice(std::vector<watt_s> &w,std::vector<wetter_s> &wetter,int ab,int bis,float preis,float &maxsoc)    // Anzahle Einträge mit > preis
{                                            // l1 = erste position h1 = letzte Position
    float x1 = 0;   // x1 = Bedarf
    float x2 = 0;   // solarer zugewinn
    float x3 = 0;   // veränderung SoC
    float x4 = 0;   // maximale Entladung = Bedarf
    maxsoc = 0; // maximal erreicher Soc
    if (wetter.size() == 0)
        return 0;
    for (int j = ab; (j <= bis)&&(j<w.size()); j++ )
    {
        x3 = wetter[j].hourly + wetter[j].wpbedarf - wetter[j].solar;;
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
                {
                    x1 = x1 + x3;
                    if (x1>=100) return x1;
                }
            }
            else 
            {
                x2 = x2 - x3;
                if (x2>maxsoc) maxsoc = x2;
// Highpricesuche abbrechen
                if (x2 > 100)
                return x1;
                    
            }
        } else
            if (x3 < 0)
            {              // PV Überschuss ?
                x2 = x2 - x3;
                if (x2>maxsoc) maxsoc = x2;
                if (x2 > 100)
                {
                    x2 = 100;
                    maxsoc = x2;
                    // Wenn der Speicher voll ist, kann abgebrochen werden?
                    return x1;
                }
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
int suchenSolar(std::vector<wetter_s> &w,int x1,float &Verbrauch)
// Suchen Zeitpunkt für den ersten solaren Überschuss
// -1 = kein Überschuss gefunden
// ansonsten Anzahl der 15min Einträge sowie den aufgelaufenen Verbrauch
{
    
    Verbrauch = 0;
    for (;x1<w.size()&&w[x1].hourly>w[x1].solar;x1++)
    {
        Verbrauch = Verbrauch + w[x1].hourly + w[x1].wpbedarf;
    }
    return x1;
}
int SimuWATTar(std::vector<watt_s> &w, std::vector<wetter_s> &wetter, int h, float &fSoC,float &anforderung,float Diff,float aufschlag, float reserve, float ladeleistung) // fConsumption Verbrauch in % SoC Differenz Laden/Endladen

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
    float maxsoc;
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
        float Verbrauch;
// Verbrauch bis solarenÜberschuss??
        int ret = 0;
        ret = suchenSolar(wetter,h, Verbrauch) - h;

        // Überprüfen ob entladen werden kann
        if (ret<10)
            reserve = (reserve/10)*ret;
        

        fSoC = fSoC - reserve;
        fConsumption = fHighprice(w,wetter,h,w.size()-1,w[h].pp,maxsoc);  // wieviel Einträge sind höher mit dem SoC in Consumption abgleichen
        float faval = fSoC-fConsumption;
        if (faval >=0) // x1 Anzahl der Einträge mit höheren Preisen
        {
            if (faval < -anforderung)
                anforderung = -faval;
                fSoC = fSoC + anforderung + reserve;
                return 1;
        } 
/*        else
        {
                fSoC = fSoC + faval;
                anforderung = anforderung + faval;
        }
*/
        // suche über den gesamten Bereich
        x1 = SucheDiff(w,h, aufschlag,Diff); // es wird gandenlos bis zum nächsten low entladen
        do
        {
            fConsumption = fHighprice(w,wetter,h,l1,w[h].pp,maxsoc);  // nächster Nachladepunkt überprüfen
//            if (float(fSoC-fConsumption+reserve) > 0) // x1 Anzahl der Einträge mit höheren Preisen
            if (float(fSoC-fConsumption+reserve) > 0) // x1 Anzahl der Einträge mit höheren Preisen
                if ((w[h].pp>w[l1].pp*aufschlag+Diff)&&fConsumption<fSoC)
                    // Es könnte nachgeladen werden
                {
                    fSoC = fSoC + anforderung + reserve;
                    if (fSoC < 0)
                        fSoC = 0;
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
                
                SollSoc = fHighprice(w,wetter,h,l1,w[h].pp*aufschlag+Diff,maxsoc);  // Preisspitzen, es muss mindestens eine vorliegen
                float SollSoc2 = fHighprice(w,wetter,h,w.size()-1,w[h].pp*aufschlag+Diff,maxsoc);

                if (SollSoc2 < fSoC)
                {
                    fSoC = fSoC + reserve;
                    return 0;
                }
                // Nachladen aus dem Netz erforderlich, wenn für die Abdeckung der Preisspitzen
                // Stunden mit hohen Börsenpreisen, Nachladen wenn SoC zu niedrig
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
                    fSoC = fSoC + reserve;
                    return 2;
                }
                else
                    //                if ((SollSoc)>fSoC-1)
                {
                    fSoC = fSoC + reserve;
                    return 0;
                } // Nicht entladen da die Preisdifferenz zur Spitze noch zu groß
            }
        }
        //        if (taglaenge > Wintertag) // tagsüber noch hochpreise es werden mind. die 2h nach sonnaufgang geprüft
        {
            fConsumption = fHighprice(w,wetter,h,w.size()-1,w[h].pp,maxsoc);  // folgender Preis höher, dann anteilig berücksichtigen
            
            
//            if (float(fSoC-fConsumption+reserve) > 0) // x1 Anzahl der Einträge mit höheren Preisen
            if (float(fSoC-fConsumption) >=0) // x1 Anzahl der Einträge mit höheren Preisen
            {
                fSoC = fSoC + reserve;
                return 1;
            }
        }
        fSoC = fSoC + reserve;
        return 0;  // kein Ergebniss gefunden
        
        
    }
}

int CheckaWATTar(std::vector<watt_s> &w,std::vector<wetter_s> &wetter, float fSoC,float fmaxSoC,float fConsumption,float Diff,float aufschlag, float ladeleistung,int mode,float &fstrompreis, float Reserve) // fConsumption Verbrauch in % SoC Differenz Laden/Endladen

// Returncode 1 = keine Aktion, 0 Batterieentladen stoppen 2 Batterie mit Netzstrom laden
{
// Ermitteln Stundenanzahl
// Analyseergebnisse in die Datei schreiben
    static float lowpp;
    time_t  rawtime;
    time(&rawtime);
    struct tm * ptm;
    ptm = gmtime (&rawtime);
    float maxsoc;
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
//    int taglaenge = sunset-sunrise;
//    int tagoffset = 12*60-taglaenge;
//    tagoffset = tagoffset/2;
//    if (tagoffset < 0) tagoffset = 0;

if (mode == 0) // Standardmodus
{
    float offset;
    float SollSoc = 0;
    float Verbrauch;
// Verbrauch bis solarenÜberschuss??
    int ret = 0;
    ret = suchenSolar(wetter,0, Verbrauch);

    // Überprüfen ob entladen werden kann
    if (ret < Reserve)
        Reserve = ret;

    fSoC = fSoC - Reserve;
// Überprüfen ob entladen werden kann
        fConsumption = fHighprice(w,wetter,0,w.size()-1,w[0].pp,maxsoc);  // wieviel Einträge sind höher mit dem SoC in Consumption abgleichen
        if (float(fSoC-fConsumption) >=0)// x1 Anzahl der Einträge mit höheren Preisen
        {
            fSoC = fSoC + Reserve;
            return 1;
        }
// geändert am 30.9.
// suche über den gesamten Bereich
        x1 = SucheDiff(w,0, aufschlag,Diff); // es wird gandenlos bis zum nächsten low entladen
        do
        {
            fConsumption = fHighprice(w,wetter,0,l1,w[0].pp,maxsoc);  // nächster Nachladepunkt überprüfen
            if (float(fSoC-fConsumption+Reserve) > 0) // x1 Anzahl der Einträge mit höheren Preisen
            if ((w[0].pp>w[l1].pp*aufschlag+Diff))
            {
                fSoC = fSoC + Reserve;
                return 1;
            }
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
            SollSoc = fHighprice(w,wetter,0,l1,w[0].pp*aufschlag+Diff,maxsoc);  // Preisspitzen, es muss mindestens eine vorliegen
            float SollSoc2 = fHighprice(w,wetter,0,w.size()-1,w[0].pp*aufschlag+Diff,maxsoc);  // Preisspitzen, es muss mindestens eine                                             // Nachladen aus dem Netz erforderlich, wenn für die Abdeckung der Preisspitzen

            if (x1==x3) {
                if (SollSoc2>SollSoc)
//                    SollSoc = SollSoc2 + fSoC;
                SollSoc = SollSoc2;

            } 
//            else
//                SollSoc = SollSoc + fSoC;

//            if ((ptm->tm_hour*60+ptm->tm_min)>(sunrise)&&(ptm->tm_hour*60+ptm->tm_min)<(sunset-120)&&(SollSoc > (fmaxSoC-1)))
//                SollSoc = fmaxSoC-1;  //tagsüber laden bis 2h vor sonnenuntergang auf Reserve beschränken
            if (SollSoc > 95-Reserve) SollSoc = 95-Reserve;
            // Der Speicher soll nicht leer herumstehen, zum Tiefstkurs laden.
            if ((SollSoc < 0.5)&&(fSoC < 0.5)&&(lw==0)) SollSoc = 1;
            if ((SollSoc>fSoC+0.5)&&        // Damit es kein Überschwingen gibt, wird 2% weniger als das Soll geladen
                ((lw==0)||((SollSoc-fSoC)>x1*ladeleistung)))      // Stunden mit hohen Börsenpreisen, Nachladen wenn SoC zu niedrig
            {
                fSoC = fSoC + Reserve;
                return 2;
            }
            else
//                if ((SollSoc)>fSoC-1) 
            {
                fSoC = fSoC + Reserve;
                return 0;
            }
        }
    }
//        if (taglaenge > Wintertag) // tagsüber noch hochpreise es werden mind. die 2h nach sonnaufgang geprüft
    {
            fConsumption = fHighprice(w,wetter,0,w.size()-1,w[0].pp,maxsoc);  // folgender Preis höher, dann anteilig berücksichtigen

        
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
                offset = pow(abs(offset),3.5)*(24*60-sunriseAt);
//            if (offset < ioffset) offset = ioffset;

// Überprüfen ob entladen werden kann
            fConsumption = fHighprice(w,wetter,0,w.size()-1,w[0].pp,maxsoc);  // wieviel Einträge sind höher mit dem SoC in Consumption abgleichen
            if (float(fSoC-fConsumption) >=0) // x1 Anzahl der Einträge mit höheren Preisen
                return 1;

// suche über den gesamten Bereich
            x1 = SucheDiff(w,0, aufschlag,Diff); // es wird gandenlos bis zum nächsten low entladen
            do
            {
                fConsumption = fHighprice(w,wetter,0,l1,w[0].pp,maxsoc);  // nächster Nachladepunkt überprüfen
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
                    if ((ptm->tm_hour*60+ptm->tm_min)<(sunriseAt+offset))
                        x3 = SuchePos(w,sunriseAt+offset+60); // eine Stunde weiter suhen
                    else
                        x3 = SuchePos(w,sunriseAt+25*60+offset);
                    if (x3 > 0)
                        x3--;
                    if (x3<l1&&x3>=0) l1 = x3;
                    // SollSoC minutengenau berechnen X3 ist die letzte volle Stunde
                if (w[0].pp*aufschlag+Diff<w[l1].pp)
                    {
                        SollSoc = ((sunriseAt+int(offset))%60);
                        SollSoc = SollSoc/60;
                        SollSoc = SollSoc*(wetter[l1].hourly+wetter[l1].wpbedarf);
                    }
                }

                x1 = Lowprice(w,0, hi, w[0].pp);   // bis zum high suchen
                fConsumption = fHighprice(w,wetter,0,l1,w[0].pp*aufschlag+Diff,maxsoc);  // Preisspitzen, es muss mindestens eine vorliegen
                                                // Nachladen aus dem Netz erforderlich, wenn für die Abdeckung der Preisspitzen
    //            if (((fSoC < (x2*fConsumption+5))&&((l1==0)||(x2*fConsumption-fSoC)>x1*23))&&(fSoC<fmaxSoC-1))      // Stunden mit hohen Börsenpreisen, Nachladen wenn SoC zu niedrig
                SollSoc = SollSoc+fConsumption;
                float SollSoc2 = fSoC;
                for (int j=0;j<=x3;j++) // Simulation
                {
                    if (w[j].pp < w[0].pp&&SollSoc2<0) break; // war schon überzogen Abruch
                    if (w[j].pp < w[0].pp) SollSoc2 = SollSoc2 + ladeleistung;
                    if (w[j].pp > w[0].pp*aufschlag+Diff) SollSoc2 = SollSoc2 - wetter[j].hourly-wetter[j].wpbedarf;
                    if (SollSoc2 > fmaxSoC-1||SollSoc2<ladeleistung*-1) break;
                }
                if (SollSoc2 < 0){
                    SollSoc2 = fSoC-SollSoc2;
                    if (SollSoc2 > SollSoc)
                        SollSoc = SollSoc2;}
                if ((ptm->tm_hour*60+ptm->tm_min)>(sunriseAt)&&(ptm->tm_hour*60+ptm->tm_min)<(sunsetAt)&&(SollSoc > (fmaxSoC-1)))
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
            if ((ptm->tm_hour*60+ptm->tm_min)<(sunriseAt))  // bis Sonnenaufgang
                x2 = SuchePos(w,sunriseAt+offset+60); // Suchen bis 2h nach Sonnenaufgang
            else
            {
                if ((ptm->tm_hour*60+ptm->tm_min)<(sunriseAt+offset)) // Zwischen Sonnenaufgang + offset entladen generell freigegben
                    return 1; // Suchen bis 2h nach Sonnenaufgang
                else
                    if ((ptm->tm_hour*60+ptm->tm_min)<(sunriseAt+offset))
                        x2 = SuchePos(w,sunriseAt+offset+60);
                    else
                        x2 = SuchePos(w,sunriseAt+25*60+offset); // Nein suchen nächsten Tag bis offset + 60
            }
            if (x2<0) x2 = w.size()-1;
            if (x2 > 0)
                fConsumption = fHighprice(w,wetter,0,x2-1,w[0].pp,maxsoc);  // folgender Preis höher, dann anteilig berücksichtigen
            else
                x3=0;

            
            SollSoc = fHighprice(w,wetter,0,x2,w[0].pp,maxsoc);  // Anzahl höhere Preise ermitteln
            if (SollSoc>fConsumption)  // SollSoC minutengenau berechnen
            {
                SollSoc = ((sunriseAt+int(offset))%60);
                SollSoc = SollSoc/60;
                SollSoc = SollSoc*(wetter[x2].hourly+wetter[x2].wpbedarf)+fConsumption;
            }
        if (float(fSoC-SollSoc) >=0) // x1 Anzahl der Einträge mit höheren Preisen
        return 1;
        }

        return 0;  // kein Ergebniss gefunden

    }
    return 0;
}
int fp_status = -1;  //

void openmeteo(std::vector<watt_s> &w,std::vector<wetter_s>  &wetter, e3dc_config_t &e3dc,int anlage,u_int32_t iDayStat[25*4*2+1])
{
    FILE * fp;
    char line[1024];
    char path [65000];
    char value[25];
    char var[25];
    char var2[25];

    int x1 = 0;
    int x2 = 0;
    int x3 = 0;
    if (w.size()==0) 
    {
        printf("keine Börsenpreise");
        return;
    }
    if (wetter.size()==0) 
    {
        printf("keine Wetterdaten");
        return;
    }
    int len = strlen(e3dc.Forecast[anlage]);
    
    if (anlage >=0)
    {
        if (len>sizeof(line)||len==0)
        {
//            printf("forecast #%i kann nicht verarbeitet werden ",anlage+1);
            return;
        }
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
        if (x1>0&&x2-x1-1>0&&len-x2-1>0)
        {
            memcpy(&var,&line[0],x1);
            memcpy(&var2,&line[x1+1],x2-x1-1);
            memcpy(&value,&line[x2+1],len-x2-1);
        }
        else
            return;
        x1 = atoi(var);
        x2 = atoi(var2);
        x3 = atoi(value);
    }

   if (x3>0)
      {
          if (e3dc.debug)
              printf("om.1\n");
          sprintf(line,"curl -s -X GET 'https://api.open-meteo.com/v1/forecast?latitude=%f&longitude=%f&minutely_15=global_tilted_irradiance_instant&timeformat=unixtime&forecast_minutely_15=192&tilt=%i&azimuth=%i'",e3dc.hoehe,e3dc.laenge,x1,x2);

          fp = NULL;
          fp = popen(line, "r");
          int fd = fileno(fp);
          int flags = fcntl(fd, F_GETFL, 0);
          flags |= O_NONBLOCK;
          fcntl(fd, F_SETFL, flags);
          fp_status = 2;

        int timeout = 0;
          if (e3dc.debug)
              printf("om.2\n");
          if (fp != NULL)
          while (fgets(path, sizeof(path), fp) == NULL&&timeout < 30)
        {
            sleep(1);
            timeout++;
        }
          if (e3dc.debug)
              printf("om%i.3\n",timeout);

          if (timeout >= 30)
          {
              if (fp!=NULL) pclose(fp);
              return;
          }
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
            int x2 = 0;
            if (e3dc.debug)
                printf("om.4\n");
            while (item1!=NULL)
            {
                if (w.size()>0)
//                while (w[x1].hh < item1->valueint&&x1<w.size())
//                    x1++;
                while (wetter[x2].hh < item1->valueint&&x2<wetter.size())
                    x2++;
                if (x2 >= wetter.size())
                    break;
                if (wetter[x2].hh == item1->valueint)
                {
                    // index 200 heutiger Ertrag 15min
                    // index 199 heutige Prognose kumuliert
                    // Index 198 heutiger Ertrag kumuliert

                    int y1 = wetter[x2].hh%(24*3600)/900;
                    float f2 = iDayStat[y1]/100.0;  // Soll
                    float f3 = iDayStat[y1+96]/(e3dc.speichergroesse*10*3600);  //Ist
                    // aktuelle PV-Leistung ermitteln aus Prog
                    float f4 = (iDayStat[199]) * e3dc.speichergroesse/10000.0;
                    float f5 = iDayStat[198]/3600.0/1000.0;
                    float f6 = 1;
                    if (f4>0&&(wetter[x2].hh-wetter[0].hh)<12*3600)
                        f6 = f5/f4;
                    if (f6<0.1) f6 = 0.1;  // schneebedeckte Module?
                    if (f6>3.5) f6 = 3.5;
                    float f7 = 0;
                    if (iDayStat[y1]>0&&f2>f3)
                        f7 = f3/f2;
// absoluter Ertrag des letzen 15min
                    float f8 = iDayStat[197] /(e3dc.speichergroesse*10*3600);
                    f8 = f8 * (10 - x2)/10;
                    
                    // relativer ertrag aus statistik höher als aktueller ertrag
                    if (f4 > 1&&x2<10)
                        f6 = (f7*(x2+1)+(10-x2)*f6)/(11);
                    else
                        f6=f7;
//                    if (x2<10)
//                    f6 = (f6*(x2)+f8*(10-x2))/10;

                    if (anlage==0){
                        wetter[x2].progsolar = item2->valuedouble*x3/4/e3dc.speichergroesse/10;
                        
                            wetter[x2].solar = wetter[x2].progsolar*f6;

                        // (15min Intervall daher /4
                    }
                    else {
                            wetter[x2].progsolar = wetter[x2].progsolar+item2->valuedouble*x3/4/e3dc.speichergroesse/10;
                            wetter[x2].solar = wetter[x2].progsolar*f6;

                    }
                    int hh = wetter[x2].hh-wetter[0].hh;
                    if (wetter[x2].solar<f8&&wetter[x2].progsolar*f6>f8
                        &&
                        (wetter[x2].hh-wetter[0].hh)<12*3600)    // 12h
                            (wetter[x2].solar=(2*f8+wetter[x2].progsolar*f6)/3);
                    else
                        if (wetter[x2].solar<f8&&wetter[x2].progsolar*f6>f8
                            &&
                            (wetter[x2].hh-wetter[0].hh)<12*3600)    // 12h
                                (wetter[x2].solar=(1*f8+wetter[x2].progsolar*f6)/2);
                        else
                            if (wetter[x2].solar<f8&&wetter[x2].progsolar*f6<f8
                                && (wetter[x2].progsolar > 1) &&
                                (wetter[x2].hh-wetter[0].hh)<12*3600)    // 12h
                                    (wetter[x2].solar=(wetter[x2].progsolar*f6+f8)/2);
                            else
                        wetter[x2].solar = wetter[x2].progsolar*f6;
                    x1++;
                    x2++;
                    if (x2 > wetter.size())
                    {                        
                        if (e3dc.debug)
                            printf("om.bfc\n");
                        if (fp!=NULL) pclose(fp);
                        if (e3dc.debug)
                        printf("om.afc\n");
                        return;
                    }
                }
                item1 = item1->next;
                item2 = item2->next;

            }
            if (e3dc.debug)
                printf("om.bfc\n");
            if (fp!=NULL) pclose(fp);
            if (e3dc.debug)
            printf("om.afc\n");
            return;
        }
    }
    if (e3dc.debug)
        printf("om.5");
    return;
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
        sprintf(line,"curl -s -X GET 'https://api.forecast.solar/estimate/%f/%f/%s?time=seconds'| jq . > %s",e3dc_config.hoehe,e3dc_config.laenge,e3dc_config.Forecast[anlage],var);
        
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
                            float pv2 = wetter[w1-1].solar;
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
/*
                            if (anlage == 0)
                                w[w1-1].solar = pv;
                            else
                                w[w1-1].solar = w[w1-1].solar + pv;
*/
                        }
                            w1++;
                    }
                }
            }
        }
    };
};
            
//void aWATTar(std::vector<watt_s> &ch, int32_t Land, int MWSt, float Nebenkosten)
void aWATTar(std::vector<ch_s> &ch,std::vector<watt_s> &w,std::vector<wetter_s> &wetter, e3dc_config_t &e3dc, float soc, int sunrise, u_int32_t iDayStat[25*4*2+1])
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
    FILE * fp2;
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
    {
        if (wetter.size() == 0)
            return;
        while ((not simu)&&w.size()>0&&(w[0].hh+900<=rawtime))
            w.erase(w.begin());}
    else
        while ((not simu)&&w.size()>0&&(w[0].hh+3600<=rawtime))
        w.erase(w.begin());

// Tagesverbrauchsprofil aus e3dc.config.txt AWhourly vorbelegen


    
    if (
        oldhour == 0
        ||
        ((rawtime-oldhour)>=3600&&ptm->tm_min==1) // erst eine Minute später
        ||
        ((ptm->tm_hour>=12)&&(ptm->tm_min%5==1)&&(ptm->tm_sec==0)&&(w.size()<12))
        ||
// die Wetterdaten alle 15min in der 14ten min holen,
        (e3dc.openmeteo&&((rawtime-oldhour)>=900)&&ptm->tm_min%15==14)
        ||
        (e3dc.openmeteo&&(ptm->tm_hour>=12)&&(ptm->tm_min%5==1)&&(ptm->tm_sec==0)&&(w.size()<48))
        ||
        (w.size()==0&&e3dc.aWATTar)
        )
    {
        oldhour = rawtime;
//        old_w_size = w.size();

        for (int j1 = 0;j1<24;j1++) {
            strombedarf[j1] = e3dc.Avhourly;
        }
    // Tagesverbrauchsprofil einlesen. Nur wenn keine Statistik
if (not e3dc.statistik)
{
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

        if (e3dc.aWATTar||e3dc.unload<0)
// aWATTar Preise auch für rasdorf holen wg. statistik ausgaben
        {
        
        printf("GET api.awattar\n");


if (e3dc.AWLand == 1)
        sprintf(line,"curl -s -X GET 'https://api.awattar.de/v1/marketdata?start=%llu&end=%llu'| jq .data| jq '.[]' | jq '.start_timestamp/1000, .marketprice'> awattar.out",von,bis);
if (e3dc.AWLand == 2)
        sprintf(line,"curl -s -X GET 'https://api.awattar.at/v1/marketdata?start=%llu&end=%llu'| jq .data| jq '.[]' | jq '.start_timestamp/1000, .marketprice'> awattar.out",von,bis);
        if ((not simu)&&
            (
            (w.size()<12)
            ||
            (e3dc.openmeteo&&w.size()<48)
            )
            ) // alte aWATTar Datei verarbeiten
        {
            if (e3dc.debug)
            {
                fp = fopen("debug.out","a");
                if(!fp)
                    fp = fopen("debug.out","w");
                fprintf(fp,"%s",line);
                fclose(fp);
            }
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
//                            ww.solar = 0;
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
                    printf("GET api.awattar done\n");
                    
                };
            }
        }
    }
        if (e3dc.debug)
            printf("wetter.size = %i\n",wetter.size());
if (wetter.size()==0) return;
if (e3dc.openmeteo)
{
//    openmeteo(w, e3dc, -1);  // allgemeine Wetterdaten einlesen wie Temperatur
    for (int j=0;j<4;j++){

//        std::thread  t1(openmeteo(w,wetter, e3dc, j));

        if (e3dc.debug) printf("openmeteo%i\n",j);
        openmeteo(w,wetter, e3dc, j, iDayStat);
    }
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
            ret = CheckaWATTar(w,wetter, fSoC,fmaxSoC,fCharge,Diff,aufschlag, ladeleistung,1,strompreis,e3dc.AWReserve);
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
    if (e3dc.wallbox < 0)
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
    int chch = 0; // 0 normal 1 Automatik
    static int dauer = -1;
    if (CheckWallbox(e3dc.e3dcwallboxtxt))
    {
        fp = fopen(e3dc.e3dcwallboxtxt,"r");
        if (fp)
        {
            char * res = (fgets(line, sizeof(line), fp)); // Nur eine Zeile mit dem Angabe der Ladedauer lesen
            ladedauer = atoi(line);
            fclose(fp);
        };
        von = rawtime;
        if (von%24*3600/3600<19) von = von - von%24*3600+20*3600;
        bis = von - von%24*3600 + 44*3600;
        von = w[0].hh;
        bis = w[w.size()-1].hh;
    } else
    {
        // ist die ladezeit schon belegt oder abgelaufen
        chch = 1;
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
            } else ladedauer = 0;
        } else return;
    }

    long k;       // bis zu     if (k > 7) k = 24-k+7;
    static std::vector<ch_s> ch1;
     
        ww1.pp = -1000;
//alle alten einträge löschen ch = 1
    for (int j = 0; j < ch.size(); j++ ){
        while (ch[j].ch == chch&&ch.size()>j)
            ch.erase(ch.begin()+j);
    }
//    ch.clear();   // Alle Einträge löschen
// neue einträge erst in die ch1 bearbeiten
    for (int l = 0;(l< w.size()); l++)
    {
        if (w[l].hh>=von&&w[l].hh<=bis&&(w[l].hh||w[l].hh<=rawtime))
        {
            cc.hh = w[l].hh;
            cc.ch = chch;
            cc.pp = w[l].pp;
            ch1.push_back(cc);
        }
    }
    std::stable_sort(ch1.begin(), ch1.end(), [](const ch_s& a, const ch_s& b) {
        return a.pp < b.pp;});
    while (ch1.size()>0&&(ch1.size()>(ladedauer*4)||ch1[ch1.size()-1].hh>bis))
    {
        ch1.erase(ch1.end()-1);
    }
// ist die letzte Stunde weniger als eine ganze Stunde?
    if (ch1.size()>=4)
    {
        int la = ch1[ch1.size()-4].hh/3600;
        while (ch1.size()>=1&&ch1[ch1.size()-1].hh/3600!=la)
        {
            ch1.erase(ch1.end()-1);
        }
    }
    
    
    if (chch == 0) // Nur bei 0 ausgeben
    {
        fp = NULL;
        fp = fopen(e3dc.e3dcwallboxtxt,"w");
        if (not fp) {
            printf("die e3dc.wallbox.txt kann nicht zum Schreiben geöffnet werden");
            sleep(10);
            return;
        }
        fprintf(fp,"%i\n",ladedauer);
    }
// in zeitliche reihenfolge sortieren

    std::stable_sort(ch1.begin(), ch1.end(), [](const ch_s& a, const ch_s& b) {
        return a.hh < b.hh;});
    // Alle Elemente aus ch1 in die ch einfügen
    for (int l = 0;l<ch1.size();l++)
        ch.push_back(ch1[l]);
    ch1.clear();

    std::stable_sort(ch.begin(), ch.end(), [](const ch_s& a, const ch_s& b) {
        return a.ch < b.ch;});

    
    int ptm_alt;
    for (int j = 0; j < ch.size(); j=j+4 )
    {
        if ((j==0&&ch[j].ch==1)
            ||
            (j>0&&ch[j-1].ch==0&&ch[j].ch==1))
            if (chch==0)
                fprintf(fp,"\nVon der Ladezeitenautomatik erzeugt\n");
        
        //        fprintf(fp2,"%li %i %f \n",ch[j].hh,ch[j].ch,ch[j].pp);
        //        k = (ch[j].hh% (24*3600)/3600);
        ptm = localtime(&ch[j].hh);
        //        fprintf(fp,"%i %.2f; ",k,ch[j].pp);
        //        if (((j==0)||(j>0&&ptm->tm_mday!=ptm_alt)&&ch[j].hh%3600==0))
        if (chch==0)
        {
            if (((j==0)||(j>0&&ptm->tm_mday!=ptm_alt)))
                // Datum und Reihenfolge ausgeben
            {
                if (j%2==1) fprintf(fp,"\n");
                fprintf(fp,"am %i.%i.\n",ptm->tm_mday,ptm->tm_mon+1);
            }
            fprintf(fp,"%i. um %i:00 zu %.3fct/kWh  ",j/4+1,ptm->tm_hour,ch[j].pp*(100+e3dc.AWMWSt)/1000+e3dc.AWNebenkosten);
            if (ch.size() < 40||(j/4)%2==1)
                fprintf(fp,"\n");
            ptm_alt = ptm->tm_mday;
            
            
            if (ch.size()>=4&&ch[ch.size()-1].hh/3600!=ch[ch.size()-4].hh/3600)
                fprintf(fp,"%i. um %i:00 zu %.3fct/kWh  \n",ch.size()/4+1,ptm->tm_hour,ch[ch.size()-1].pp*(100+e3dc.AWMWSt)/1000+e3dc.AWNebenkosten);
            //    if (e3dc.wbhour>0&&chch==0)
            //        fprintf(fp,"Achtung Ladezeitenautomatik ist noch aktiv\nund kann diese Zeiten verändern\n");
        }
    }
    if (chch==0)
    {
        fprintf(fp,"%s\n",ptm->tm_zone);
        if (fp)
            fclose(fp);}
    PutWallbox(ch); // Schaltzeiten schreiben
    (CheckWallbox(e3dc.e3dcwallboxtxt));

    }
