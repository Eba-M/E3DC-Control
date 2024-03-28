//
//  E3DC_CONF.h
//  RscpExcample
//
//  Created by Eberhard Mayer on 16.08.18.
//  Copyright © 2018 Eberhard Mayer. All rights reserved.
//
#define VERSION "C2024.03.28.0" //aktuelle Version für dynamische Tarife und WP-Betrieb
#ifndef E3DC_CONF_h
#define E3DC_CONF_h


#endif /* E3DC_CONF_h */
// Konfigurationsdatei
#define CONF_FILE "e3dc.config.txt"
#define WURZELZAEHLER 0;     // 0 = interner Zähler 6 = externer Zähler

#define LADESCHWELLE 50;     // bis zur dieser Schwelle wird geladen bevor die Regelung beginnt
#define LADEENDE 80;         // Zielwert bis Ende Regelung, dannach wird Ladung auf Landeende2 weiter geregelt und dann ab SOMMERLADEENDE freigegeben
#define LADEENDE2 93;
#define UNTERERLADEKORRIDOR  100 // die Ladeleistung soll zwischen dem unteren und
#define OBERERLADEKORRIDOR  2000 // oberere Ladeleistung liegen, jedoch
#define MINIMUMLADELEISTUNG  100 // immer > MINIMUMLADELEISTUNG
#define POWERFAKTOR           -1 // Verstärkungsfaktor
#define MAXIMUMLADELEISTUNG 3000 // maximale Ladeleistung
#define WRLEISTUNG 12000 // maximale Ladeleistung
#define WBMAXLADESTROM 32 // maximale Ladestrom der Wallbox

#define SPEICHERGROESSE 13.8 // nutzbare Kapazität des S10 Speichers
#define WINTERMINIMUM   11.5 // Uhrzeit (als Dezimalwert) bis zu dieser Uhrzeit wird das Laden überwacht
#define SOMMERMAXIMUM   14.5 // alle Zeiten in GMT = MEZ Winterzeit - 1
#define SOMMERLADEENDE  18.5 // alle Zeiten in GMT = MEZ Winterzeit - 1
#define EINSPEISELIMIT   30  // maximal erlaubte Einspeiseleistung in kW
