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

int CheckaWATTar(int sunrise,int sunset,float fSoC,float fConsumption,float Diff) // fConsumption Verbrauch in % SoC Differenz Laden/Endladen

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
    if (Minuten <= sunrise)
    {
        SucheHT(0,sunrise+120); // sunrise nächster Tag suchen HT Werte
        SucheHT(0,w[l1].hh);
        if (low2.pp == 0)
        { low2 = w[l1];
            if (low2.pp > 100)
                low2.pp = 100;
        } // ist low vorbelegt?
        if (h1 < l1)              // ist noch ein h1 vor dem low?
        {
            x2 = Highprice(0,l1,low2.pp+Diff);  // liegt der Hochpreis um den Diffpreis über das kommende Tief -> Endladen
            if (x2 > 0)               // ist noch ein h1 vor dem low?
            {
                if (w[0].pp > w[l1].pp+Diff)         // Der aktuelle Wert ist > Tiefstwert + Diff  Entladen erlaubt
                {
                    x1 = Highprice(0,l1,w[0].pp);  // wieviel Einträge sind höher mit dem SoC in Consumption abgleichen
                    if ((fSoC-x1*fConsumption) > 0) // x1 Anzahl der Einträge mit höheren Preisen
                    return 1;
                }
            }
            return 0;
        } else                        // Es muss nichts mehr ausgespeichert werden
        {
            SucheHT(0,sunrise+120);            // l1 = geringster   h1 = höchster preis
            int lw = l1;                        // wenn l1 = 0, dann ist die aktuelle Stunde ein Tiefpreis zum Nachladen
            x3 = SuchePos(sunrise+120);
            x1 = Lowprice(0, x3, w[0].pp);
            x2 = Highprice(l1,x3,low2.pp+Diff);  // Preisspitzen am Morgen
                                            // Nachladen aus dem Netz erforderlich
            if ((fSoC < x2*fConsumption)&&((lw==0)||(x2*fConsumption-fSoC)>x1*23))      // Stunden mit hohen Börsenpreisen, Nachladen wenn SoC zu niedrig
                return 2; else
                {
                    if (w[0].pp>low2.pp+Diff) return 1;
                    else
                    return 0;
                }
            
        }
        return 0;
    }
    if ((Minuten> sunrise-120)&&(Minuten <= sunset-120))
    {
        return 1; //tagsüber immer entladen zulassen
    }
/*
2h vor Sonnenuntergang
Wenn nach Sonnenuntergang noch eine Preisspitze kommt, dann wird das Entladen gespeert
 */
    if ((Minuten > sunset-120)&&(Minuten <= sunset)) // 2 Stunden vor Sonnenuntergang
    {

        //        SucheHT(0,sunset+120); // tagsüber und 2h Nach Sonnenuntergang HT überprüfen
        SucheHT(0,sunrise+24*60); // bis zu nächsten Sonnenaufgang
        SucheHT(0,w[l1].hh);     // gibt es eine Preisspitze bis zum zum low?
        x2 = Highprice(0,l1,w[l1].pp+Diff);   // kann zum Tiefstpreis nachgeladen werden?
        if (x2 > 0)                           // x2 Anzahl Stunden zum entladen
            if (w[0].pp > w[l1].pp+Diff)
            {         // Der aktuelle Wert ist > Tiefstwert + Diff  Entladen erlaubt
            x1 = Highprice(0,l1,w[0].pp);           // Nur entladen wenn der SoC auch für Stunden mit höheren Preisen reicht
                if ((fSoC-x1*fConsumption) > 0)  // und >= Höchstwert
                return 1;
                
            }
        SucheHT(0,sunset+120);            // l1 = geringster   h1 = höchster preis
        int lw = l1;                        // wenn l1 = 0, dann ist die aktuelle Stunde ein Tiefpreis zum Nachladen
        x3 = SuchePos(sunset+120);
        x1 = Lowprice(0, x3, w[0].pp);
        x2 = Highprice(l1,x3,w[l1].pp+Diff);  // Preisspitzen am Abend
                                        // Nachladen aus dem Netz erforderlich
        if ((fSoC < x2*fConsumption)&&((lw==0)||(x2*fConsumption-fSoC)>x1*23))      // Stunden mit hohen Börsenpreisen, Nachladen wenn SoC zu niedrig
            return 2; else

        return 0;
    }
    if (Minuten > sunset)
    {
        SucheHT(0,sunrise+24*60); // sunrise nächster Tag suchen HT Werte
        low2 = w[l1];
        x2 = Highprice(0,l1,w[l1].pp+Diff); // Hochpreis vor low? Rückwärts suchen
        if (x2 > 0)               // ist noch ein h1 vor dem low?
        {
            if (w[0].pp > w[l1].pp+Diff)         // Der aktuelle Wert ist > Tiefstwert + Diff  Entladen erlaubt
            {
                x1 = Highprice(0,l1,w[0].pp);  // wieviel Einträge sind höher mit dem SoC in Consumption abgleichen
                if ((fSoC-x1*fConsumption) > 0) // x1 Anzahl der Einträge mit höheren Preisen
                return 1;
            }
        } else                        // Es muss nichts mehr ausgespeichert werden
        {
            SucheHT(0,sunrise+120+24*60);            // l1 = geringster   h1 = höchster preis
            int lw = l1;                        // wenn l1 = 0, dann ist die aktuelle Stunde ein Tiefpreis zum Nachladen
            x3 = SuchePos(sunrise+120+24*60);
            x1 = Lowprice(0, x3, w[0].pp);
            x2 = Highprice(l1,x3,w[l1].pp+Diff);  // Preisspitzen am Morgen
            if (x2 > 0)
            {
// Nachladen aus dem Netz erforderlich
                if ((fSoC < x2*fConsumption)&&((lw==0)||(x2*fConsumption-fSoC)>x1*23))      // Stunden mit hohen Börsenpreisen, Nachladen wenn SoC zu niedrig
                {   return 2;
            
                }
            } else // vorhandene Kapazität optimal zum Ausspeichern nutzen
            {
                x1 = Highprice(0,x3,w[0].pp);  // Gibt es mehr Stunden mit höheren Preisen wg. Reserve +1
                if ((fSoC-x1*fConsumption) > 0)  // ausreichend Speicher gefüllt -> entladen zulassen
                    return 1;
                    else return 0;
            }
        }
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
    float pp;
    FILE * fp;
    char line[256];
    time(&rawtime);
    ptm = gmtime (&rawtime);
    if ((ptm->tm_hour!=oldhour))
    {
    oldhour = ptm->tm_hour;

//    system("curl -X GET 'https://api.awattar.de/v1/marketdata'| jq .data| jq '.[]' | jq '.start_timestamp%86400000/3600000, .marketprice'> awattar.out");
// es wird der orginale Zeitstempel übernommen um den Ablauf des Zeitstempels zu erkennen
    system("curl -X GET 'https://api.awattar.de/v1/marketdata'| jq .data| jq '.[]' | jq '.start_timestamp/1000, .marketprice'> awattar.out");
    
    system ("pwd");
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

    ptm = gmtime ( &w[0].hh);
    int k = ptm->tm_hour;
    if (k > 7) k = 24-k+7;
    else k = 7;   // Es wird nur bis 7 Uhr nächsten Tag berücksichtigt
    if (k >w.size()) k = w.size();
    // ersten wert hinzufügen
    
        ww = low;
        pp = ww.pp;
        ch.clear();
    if (k>0&&ladedauer>0)
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
        pp = ww.pp;
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
    }
