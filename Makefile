CXX=g++
ROOT_VALUE=E3DC

all: $(ROOT_VALUE)

$(ROOT_VALUE): clean
	$(CXX) -O3 RscpExampleMain.cpp RscpProtocol.cpp AES.cpp SocketConnection.cpp awattar.cpp -o SunriseCalc.cpp $@


clean:
	-rm $(ROOT_VALUE) $(VECTOR)
