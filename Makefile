CXX=g++
ROOT_VALUE=E3DC-Control

all: $(ROOT_VALUE)

$(ROOT_VALUE): clean
	$(CXX) -O3 RscpExampleMain.cpp RscpProtocol.cpp AES.cpp SocketConnection.cpp awattar.cpp  SunriseCalc.cpp -o $@


clean:
	-rm $(ROOT_VALUE) $(VECTOR)
