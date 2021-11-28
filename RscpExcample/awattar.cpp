//
//  aWATTar.cpp
//  RscpExcample
//
//  Created by Eberhard Mayer on 26.11.21.
//  Copyright © 2021 Eberhard Mayer. All rights reserved.
//

#include "aWATTar.hpp"
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <array>
#include <vector>

//typedef struct {int hh; float pp;}watt_s;

void aWATTar ()

{

    std::vector<watt_s> w;
    int ladedauer = 4;
    std::vector<watt_s> ch;  //charge hour
    watt_s ww;
    
    
    double_t pp;
    
    FILE * fp;
    system("curl -X GET 'https://api.awattar.de/v1/marketdata'| jq .data| jq '.[]' | jq '.start_timestamp%86400000/3600000, .marketprice'> awattar.out");

    fp = fopen("awattar.out","r");
    char line[256];
    while (fgets(line, sizeof(line), fp)) {

        ww.hh = atoi(line);
//        h.push_back(hh);
        if (fgets(line, sizeof(line), fp)) {
            ww.pp = atof(line);
            w.push_back(ww);
        } else break;
    }

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
    int k = w[0].hh;
    if (k > 7) k = 24-k+7;
    if (k >w.size()) k = w.size();
// ersten wert hinzufügen
    ww = low;
    ch.push_back(ww);
    for (int l = 1;l < ladedauer; l++)
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
        

    }
    w.clear();
};
