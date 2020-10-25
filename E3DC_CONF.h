//
//  E3DC_CONF.h
//  RscpExcample
//
//  Created by Eberhard Mayer on 16.08.18.
//  Copyright © 2018 Eberhard Mayer. All rights reserved.
//
#define VERSION "2020.10.25.0" //Wallbox branch
#ifndef E3DC_CONF_h
#define E3DC_CONF_h


#endif /* E3DC_CONF_h */
// Konfigurationsdatei
#define CONF_FILE "e3dc.config.txt"
#define OPENWB "localhost"

#define WURZELZAEHLER 0;     // 0 = interner Zähler 6 = externer Zähler

#define LADESCHWELLE 50;     // bis zur dieser Schwelle wird geladen bevor die Regelung beginnt
#define LADEENDE 80;         // Zielwert bis Ende Regelung, dannach wird Ladung auf Landeende2 weiter geregelt und dann ab SOMMERLADEENDE freigegeben
#define LADEENDE2 93;
#define UNTERERLADEKORRIDOR  900 // die Ladeleistung soll zwischen dem unteren und
#define OBERERLADEKORRIDOR  1500 // oberere Ladeleistung liegen, jedoch
#define MINIMUMLADELEISTUNG  500 // immer > MINIMUMLADELEISTUNG
#define MAXIMUMLADELEISTUNG 3000 // maximale Ladeleistung
#define WRLEISTUNG 12000 // maximale Ladeleistung

#define SPEICHERGROESSE 13.8 // nutzbare Kapazität des S10 Speichers
#define WINTERMINIMUM   11.5 // Uhrzeit (als Dezimalwert) bis zu dieser Uhrzeit wird das Laden überwacht
#define SOMMERMAXIMUM   14.5 // alle Zeiten in GMT = MEZ Winterzeit - 1
#define SOMMERLADEENDE  18.5 // alle Zeiten in GMT = MEZ Winterzeit - 1
#define EINSPEISELIMIT   6.9 // maximal erlaubte Einspeiseleistung in kW


//const int cLadeschwelle = LADESCHWELLE; // Minimum Lade-Schwelle wird bevorzugt der E3DC-Speicher geladen
//const int cLadeende = LADEENDE;     // Lade-Schwelle des überwachten Ladens
typedef struct {
    char server_ip[20];
    uint32_t  server_port;
    char e3dc_user[128];
    char e3dc_password[128];
    char aes_password[128];
    char logfile[128],conffile[128];
    char openWBhost[128];
    bool wallbox,ext1,ext2,ext3,ext7,debug,htsat,htsun,openWB;
    uint8_t wurzelzaehler,ladeschwelle, ladeende,ladeende2, unload;
    int32_t ht, untererLadekorridor, obererLadekorridor, minimumLadeleistung, maximumLadeleistung, wrleistung,peakshave,peakshsoc,wbmode,wbminlade;
    float_t speichergroesse,winterminimum, sommermaximum,sommerladeende, einspeiselimit,
    hton, htoff, htsockel;
    
    
}e3dc_config_t;
