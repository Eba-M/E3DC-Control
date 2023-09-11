//
//  Waermepumpe.hpp
//  E3DC-V1
//
//  Created by Eberhard Mayer on 11.09.23.
//

#ifndef Waermepumpe_hpp
#define Waermepumpe_hpp

#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <vector>
#endif /* Waermepumpe_hpp */

typedef struct {time_t hh; float temp; int sky; float uvi;} wetter_s; // timestamp, Temperatur, %Bewölkung, UV-Index
int call_wp;
