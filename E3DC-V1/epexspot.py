import sys
mday = 1
marketarea = "DE-LU"
n = len(sys.argv)
if n>1:
    mday = int(sys.argv[1])
if n>2:
    marketarea = (sys.argv[2])

from bs4 import BeautifulSoup, PageElement
import requests
#print (sys.argv[1])
from datetime import date
from datetime import timedelta
date = date.today()
#print (date)
mydate = date + timedelta(days=mday)
fp = open("epexspot.txt","w")
fp.write(str(mydate)+'\n')
main_url = "http://www.epexspot.com/en/market-data?market_area="
main_url = main_url + marketarea + "&delivery_date="
main_url = main_url + str(mydate) + "&modality=Auction&sub_modality=DayAhead&product=15&data_mode=table"
print (main_url)
headers = {'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/111.0.0.0 Safari/537.36'}
#headers = {'Mozilla/5.0 (platform; rv:gecko-version) Gecko/gecko-trail Firefox/firefox-version'}
try:
        req = requests.get(main_url,headers=headers,verify=False)
except requests.exceptions.SSLError as err:
        print(err)
else:
        print("SSL Certificate is valid")

#print (req.text)
soup = BeautifulSoup(req.text, "html.parser")
# Finding the main title tag.
title = soup.find("table", class_="table-01 table-length-1")
if title.cdata_list_attributes is None:
        print ('keine Werte gefunden')
else:
        line = title.contents
#        print(len(line))

        td = title.find_all("tr", class_="child")
# für jedes Element
        y = 0;
        for x in td:
                fp.write(str(y)+' '+str(x.contents[7].string)+'\n')
                y=y+1
        fp.close()
#main_url = "https://www.eex.com/de/marktdaten/market-data-hub/strom/futures#%7B%22snippetpicker%22%3A%2228%22%7D:~:text=Wochenende-,Tag,-Base"
#req = requests.get(main_url,headers=headers)
#print (req.text)
#soup = BeautifulSoup(req.text, "html.parser")
#td = soup.find_all("tr", class_="child")

#print (td)
