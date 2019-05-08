FROM node:latest
#FROM arm32v6/alpine:3.7

# Surpress Upstart errors/warning
RUN dpkg-divert --local --rename --add /sbin/initctl
RUN ln -sf /bin/true /sbin/initctl

# Let the container know that there is no tty
ENV DEBIAN_FRONTEND noninteractive

# Install software requirements
RUN apt-get update && \
apt-get install -y git
RUN npm install -g node-gyp
RUN apt-get install build-essential

#-- C++ Install
RUN apt-get install g++ make gcc  
#RUN apt-get purge -y --auto-remove gcc libc6-dev make

# Clone the conf files into the docker contain
WORKDIR /home/e3dc-control

RUN git clone https://github.com/Eba-M/E3DC-Control.git /home/e3dc-control
COPY e3dc.config.txt.template /home/config/e3dc.config.txt
RUN g++ -o E3DC_CONTROL RscpExampleMain.cpp RscpProtocol.cpp AES.cpp SocketConnection.cpp
RUN ./E3DC_CONTROL
