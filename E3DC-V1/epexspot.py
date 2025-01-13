#!/usr/bin/env python3

from bs4 import BeautifulSoup
import requests
from datetime import date
from datetime import timedelta
date = date.today()
mydate = date + timedelta(days=1)
print (mydate)
main_url = "https://www.epexspot.com/en/market-data?market_area=DE-LU&delivery_date="
main_url = main_url + str(mydate) + "&modality=Auction&sub_modality=DayAhead&product=60&data_mode=table"
headers = {'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/111.0.0.0 Safari/537.36'}
req = requests.get(main_url,headers=headers)
soup = BeautifulSoup(req.text, "html.parser")
title = soup.find("table", class_="table-01 table-length-1")
if title.cdata_list_attributes is None:
        print ('keine Werte gefunden')
else:
        line = title.contents


        td = title.find_all("tr", class_="child")
# für jedes Element
        y = 0;
        for x in td:
                print(y,end=' ');print(x.contents[7].string)
                y=y+1

